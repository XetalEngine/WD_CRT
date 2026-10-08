#pragma once

#include "actor_registry.h"

#include <cstddef>
#include <cstdint>

namespace wdgs::world_actors
{

    enum : std::size_t
    {
        max_label_length = 64,
        max_vehicles = 64,
        max_dropped_items = 128
    };

    struct Snapshot
    {
        std::uintptr_t address = 0;
        FNameValue name{};
        FVector world_position{};
        float distance_meters = 0.f;
        actors::Kind kind = actors::Kind::none;
        char label[max_label_length]{};
    };

    bool try_build(std::uintptr_t actor, const FVector& observer_position, FNameValue actor_name, actors::Match match, float max_distance_meters, Snapshot& output);

} // namespace wdgs::world_actors
