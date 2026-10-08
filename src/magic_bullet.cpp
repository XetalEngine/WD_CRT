#include "magic_bullet.h"
#include "projectile_subsystem.h"
#include <cmath>

namespace
{
    bool finite(const FVector& value)
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    bool retarget(const wdgs::projectile_subsystem::Instance& instance, const FVector& target, std::uint32_t pawn, std::uint32_t vehicle)
    {
        const auto local = [pawn, vehicle](std::uint32_t index)
        {
            return index != 0 && (index == pawn || index == vehicle);
        };
        if (!local(instance.owner_internal_index) && !local(instance.weapon_internal_index))
            return false;
        if (instance.landed || instance.external_movement || !std::isfinite(instance.flight_time) || instance.flight_time <= 0 || !finite(instance.location) || !finite(instance.velocity))
            return false;
        const double speed = instance.velocity.Length();
        const FVector delta = target - instance.location;
        const double distance = delta.Length();
        if (!std::isfinite(speed) || speed < 100 || speed > 200000 || !std::isfinite(distance) || distance < 25)
            return false;
        return write<FVector>(instance.address + wdgs::projectile_subsystem::velocity_offset, delta * (speed / distance));
    }
} // namespace

void wdgs::magic_bullet::reset()
{
    projectile_subsystem::reset();
}

bool wdgs::magic_bullet::probe(const FVector&)
{
    projectile_subsystem::Snapshot pool{};
    return projectile_subsystem::acquire(pool);
}

bool wdgs::magic_bullet::retarget_all(const FVector& target, const FVector&, std::uint32_t pawn, std::uint32_t vehicle)
{
    if (!finite(target) || !pawn)
        return false;
    projectile_subsystem::Snapshot pool{};
    if (!projectile_subsystem::acquire(pool))
        return false;
    bool changed = false;
    for (std::uint32_t slot = 0; slot < pool.allocated; ++slot)
    {
        projectile_subsystem::Instance instance{};
        if (projectile_subsystem::read_instance(pool, slot, instance))
            changed = retarget(instance, target, pawn, vehicle) || changed;
    }
    return changed;
}
