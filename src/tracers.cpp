#include "tracers.h"
#include "visual_math.h"

namespace
{
    struct Shot
    {
        std::uintptr_t pool = 0;
        std::uint32_t slot = 0;
        double flight = 0, birth = 0, last = 0, point_time = 0;
        game::Snapshot::Trail trail;
    } shots[24];
    ULONGLONG next_sample = 0;
    unsigned sequence = 0;
    bool active = false;
} // namespace

void tracers::reset()
{
    for (auto& shot : shots)
        shot = {};
    active = false;
    sequence = 0;
    next_sample = 0;
}

void tracers::sample(const wdgs::projectile_subsystem::Instance& round, std::uintptr_t pool, double now)
{
    if (!visual_math::finite(round.location) || !std::isfinite(round.flight_time) || round.flight_time <= 0 || round.external_movement)
        return;
    Shot* slot = nullptr;
    Shot* oldest = &shots[0];
    for (auto& shot : shots)
    {
        if (shot.pool == pool && shot.slot == round.slot && round.flight_time >= shot.flight && now >= shot.last && now - shot.last < 0.1 && std::fabs(now - round.flight_time - shot.birth) < 0.05)
            slot = &shot;
        if (!shot.pool || shot.last < oldest->last)
            oldest = &shot;
    }
    if (!slot)
    {
        if (round.landed)
            return;
        slot = oldest;
        *slot = {};
        slot->pool = pool;
        slot->slot = round.slot;
        slot->birth = now - round.flight_time;
        slot->trail.hue = std::fmod(++sequence * 0.61803398875f, 1.f);
    }
    auto& trail = slot->trail;
    const double distance = trail.count ? trail.points[trail.count - 1].Distance(round.location) : 0;
    if (distance > 100000)
    {
        *slot = {};
        return;
    }
    if (!trail.count || distance > 75 || now - slot->point_time >= 0.032)
    {
        if (trail.count == trail.capacity)
        {
            for (int i = 1; i < trail.count - 1; i += 2)
                trail.points[(i + 1) / 2] = trail.points[i + 1];
            trail.count = trail.count / 2;
        }
        trail.points[trail.count++] = round.location;
        slot->point_time = now;
    }
    slot->last = now;
    slot->flight = round.flight_time;
}

void tracers::snapshot(game::Snapshot& output, double now, float lifetime)
{
    output.trails.clear();
    output.trails.reserve(24);
    for (auto& shot : shots)
    {
        const double age = now - shot.last;
        if (!shot.pool || age < 0 || age > lifetime)
        {
            shot = {};
            continue;
        }
        if (shot.trail.count < 2)
            continue;
        auto trail = shot.trail;
        trail.alpha = std::clamp(static_cast<float>((lifetime - age) / std::min(lifetime, 0.5f)), 0.f, 1.f);
        output.trails.push_back(trail);
    }
}

void tracers::tick(game::Snapshot& output, std::uint32_t pawn, std::uint32_t vehicle, bool enabled, float lifetime)
{
    if (!enabled)
    {
        if (active)
            reset();
        output.trails.clear();
        return;
    }
    active = true;
    if (pawn && frame_ticks >= next_sample)
    {
        next_sample = frame_ticks + 16;
        wdgs::projectile_subsystem::Snapshot pool;
        if (wdgs::projectile_subsystem::acquire(pool))
        {
            const auto mine = [=](std::uint32_t id)
            { return id && (id == pawn || id == vehicle); };
            int count = 0;
            for (std::uint32_t i = 0; i < pool.allocated && count < 24; ++i)
            {
                if (!pool.active(i))
                    continue;
                const auto address = pool.pool + i * wdgs::projectile_subsystem::instance_stride;
                if (!mine(read<std::uint32_t>(address + wdgs::projectile_subsystem::owner_internal_index_offset)) && !mine(read<std::uint32_t>(address + wdgs::projectile_subsystem::weapon_internal_index_offset)))
                    continue;
                wdgs::projectile_subsystem::Instance round;
                if (wdgs::projectile_subsystem::read_instance(pool, i, round) && !round.landed)
                {
                    sample(round, pool.pool, frame_time);
                    ++count;
                }
            }
        }
    }
    snapshot(output, frame_time, lifetime);
}
