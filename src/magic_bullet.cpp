#include "magic_bullet.h"
#include "projectile_subsystem.h"
#include <cmath>

namespace
{
    struct Seen
    {
        std::uintptr_t pool = 0;
        double time = 0, birth = 0;
        bool redirected = false;
    };
    Seen seen[wdgs::projectile_subsystem::max_allocated]{};
    bool finite(const FVector& value)
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    enum Result
    {
        other_owner,
        landed,
        external,
        no_flight,
        invalid_trajectory,
        write_failed,
        redirected,
        result_count
    };

    Result retarget(const wdgs::projectile_subsystem::Instance& instance, const FVector& target, std::uint32_t pawn, std::uint32_t vehicle)
    {
        const auto local = [pawn, vehicle](std::uint32_t index)
        {
            return index != 0 && (index == pawn || index == vehicle);
        };
        if (!local(instance.owner_internal_index) && !local(instance.weapon_internal_index))
            return other_owner;
        if (instance.landed)
            return landed;
        if (instance.external_movement)
            return external;
        if (!std::isfinite(instance.flight_time) || instance.flight_time <= 0)
            return no_flight;
        if (!finite(instance.location) || !finite(instance.velocity))
            return invalid_trajectory;
        const double speed = instance.velocity.Length();
        const FVector delta = target - instance.location;
        const double distance = delta.Length();
        if (!std::isfinite(speed) || speed < 100 || speed > 200000 || !std::isfinite(distance) || distance < 25)
            return invalid_trajectory;
        return write<FVector>(instance.address + wdgs::projectile_subsystem::velocity_offset, delta * (speed / distance)) ? redirected : write_failed;
    }
} // namespace

void wdgs::magic_bullet::reset()
{
    projectile_subsystem::reset();
    for (auto& item : seen)
        item = {};
}

bool wdgs::magic_bullet::probe(const FVector&)
{
    projectile_subsystem::Snapshot pool{};
    return projectile_subsystem::acquire(pool);
}

bool wdgs::magic_bullet::retarget_all(const FVector& target, const FVector&, std::uint32_t pawn, std::uint32_t vehicle, bool once, bool report)
{
    if (!finite(target) || !pawn)
    {
        if (report)
            log("Magic: invalid target or local pawn index (pawn=%u)", pawn);
        return false;
    }
    projectile_subsystem::Snapshot pool{};
    if (!projectile_subsystem::acquire(pool))
    {
        if (report)
            log("Magic: projectile pool unavailable (subsystem=%p pool=%p slots=%u)", reinterpret_cast<void*>(pool.subsystem), reinterpret_cast<void*>(pool.pool), pool.allocated);
        return false;
    }
    bool changed = false;
    unsigned counts[result_count]{};
    unsigned active = 0, unreadable = 0;
    for (std::uint32_t slot = 0; slot < pool.allocated; ++slot)
    {
        projectile_subsystem::Instance instance{};
        if (report && pool.active(slot))
            ++active;
        if (!projectile_subsystem::read_instance(pool, slot, instance))
        {
            if (report && pool.active(slot))
                ++unreadable;
            seen[slot] = {};
            continue;
        }
        auto& previous = seen[slot];
        if (previous.pool != pool.pool || instance.flight_time < previous.time || std::fabs((frame_time - instance.flight_time) - previous.birth) > 0.1)
            previous = {pool.pool, 0, frame_time - instance.flight_time, false};
        previous.time = instance.flight_time;
        if (once && previous.redirected)
            continue;
        const auto result = retarget(instance, target, pawn, vehicle);
        if (report)
            ++counts[result];
        if (result == redirected)
        {
            previous.redirected = true;
            changed = true;
        }
    }
    if (report)
        log("Magic: active=%u unreadable=%u other-owner=%u landed=%u external=%u no-flight=%u invalid-trajectory=%u write-failed=%u redirected=%u pawn=%u vehicle=%u", active, unreadable, counts[other_owner], counts[landed], counts[external], counts[no_flight], counts[invalid_trajectory], counts[write_failed], counts[redirected], pawn, vehicle);
    return changed;
}

#ifdef WD_TEST
bool wdgs::magic_bullet::test_retarget(const wdgs::projectile_subsystem::Instance& round, const FVector& target, std::uint32_t pawn, std::uint32_t vehicle)
{
    return retarget(round, target, pawn, vehicle) == redirected;
}
#endif
