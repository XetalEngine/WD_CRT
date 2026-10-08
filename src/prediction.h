#pragma once

#include "structs.h"

namespace prediction
{

    struct Input
    {
        FVector camera{};
        FVector target{};
        FVector target_velocity{};
        float bullet_speed = 0.f;
        float zeroing_meters = 100.f;
        float gravity_scale = 1.f;
        bool velocity_lead = true;
        bool bullet_drop = true;
    };

    struct Result
    {
        FVector aim_point{};
        float travel_time = 0.f;
        float drop = 0.f;
        bool valid = false;
    };

    Result solve(const Input& input) noexcept;

} // namespace prediction
