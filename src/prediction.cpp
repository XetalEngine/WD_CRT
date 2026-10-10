#include "stdafx.h"
#include "prediction.h"

#include <cmath>
#include <algorithm>

namespace
{

    bool finite_vector(const FVector& value) noexcept
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

} // namespace

prediction::Result prediction::solve(const Input& input, bool projectile) noexcept
{
    Result output{};
    if (!finite_vector(input.camera) || !finite_vector(input.target) || !finite_vector(input.target_velocity) || !std::isfinite(input.bullet_speed) || input.bullet_speed < (projectile ? 100.f : 1000.f) || input.bullet_speed > 300000.f || !std::isfinite(input.zeroing_meters) || !std::isfinite(input.gravity_scale))
        return output;

    FVector velocity = input.velocity_lead ? input.target_velocity : FVector{};
    if (!projectile && std::abs(velocity.Z) < 300.0)
        velocity.Z = 0.0;

    const double gravity = input.bullet_drop
                               ? 980.0 * static_cast<double>(std::clamp(input.gravity_scale, 0.f, 3.f))
                               : 0.0;
    const double zeroing_uu = std::clamp(static_cast<double>(input.zeroing_meters), 0.0, 2000.0) * 100.0;
    const double speed = static_cast<double>(input.bullet_speed);
    double zeroing_slope = 0.0;
    // Replacing a projectile's velocity replaces its original sight zeroing too.
    if (!projectile && gravity > 0.0 && zeroing_uu > 0.0)
    {
        const double zeroing_time = zeroing_uu / speed;
        const double zeroing_drop = 0.5 * gravity * zeroing_time * zeroing_time;
        zeroing_slope = zeroing_drop / zeroing_uu;
    }

    FVector predicted = input.target;
    double travel = 0.0;
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        const double distance = (std::max)((predicted - input.camera).Length(), 1.0);
        travel = distance / speed;
        if (!std::isfinite(travel) || travel < 0.0 || travel > 10.0)
            return output;

        predicted.X = input.target.X + velocity.X * travel;
        predicted.Y = input.target.Y + velocity.Y * travel;

        const double drop = 0.5 * gravity * travel * travel;
        const double zeroing_compensation = zeroing_slope > 0.0 ? zeroing_slope * std::hypot(predicted.X - input.camera.X, predicted.Y - input.camera.Y) : 0.0;
        predicted.Z = input.target.Z + velocity.Z * travel + drop - zeroing_compensation;
    }

    if (!finite_vector(predicted))
        return output;

    output.aim_point = predicted;
    output.travel_time = static_cast<float>(travel);
    output.drop = static_cast<float>(0.5 * gravity * travel * travel);
    output.valid = true;
    return output;
}
