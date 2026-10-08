#pragma once

#include "structs.h"

#include <cstddef>
#include <cstdint>

namespace wdgs::actors
{

    enum class Kind : std::uint8_t
    {
        none,
        player,
        dropped_item,
        heli,
        sph2,
        tank_la26,
        apc,
        buggy,
        truck,
        boat,
        motorcycle,
        stationary,
        backpack
    };

    struct Match
    {
        Kind kind = Kind::none;
        const char* label = nullptr;
    };

    void reset();
    bool resolve_names();
    Match classify(FNameValue actor_name);
    std::uint32_t comparison_index(Kind kind);
    bool is_vehicle(Kind kind);

    const char* try_get_vehicle_label(std::uintptr_t actor);
    const char* try_get_stationary_vehicle_label(std::uintptr_t actor);

} // namespace wdgs::actors
