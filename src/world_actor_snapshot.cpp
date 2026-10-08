#include "stdafx.h"
#include "world_actor_snapshot.h"

#include "offsets.h"

#include <cmath>
#include <cstring>

namespace wdgs::world_actors
{

    bool try_build(std::uintptr_t actor, const FVector& observer_position, FNameValue actor_name, actors::Match match, float max_distance_meters, Snapshot& output)
    {
        output = {};
        if (match.kind == actors::Kind::none || !match.label || !is_valid_ptr(reinterpret_cast<void*>(actor)) || !std::isfinite(observer_position.X) || !std::isfinite(observer_position.Y) || !std::isfinite(observer_position.Z) || !std::isfinite(max_distance_meters) || max_distance_meters <= 0.f)
            return false;

        const std::uintptr_t root_component = read<std::uintptr_t>(actor + offsets::AActor::RootComponent);
        if (!is_valid_ptr(reinterpret_cast<void*>(root_component)))
            return false;

        const FTransform transform = read<FTransform>(root_component + offsets::USceneComponent::ComponentToWorld);
        if (!std::isfinite(transform.Translation.X) || !std::isfinite(transform.Translation.Y) || !std::isfinite(transform.Translation.Z))
            return false;

        const double distance_meters = observer_position.Distance(transform.Translation) / 100.0;
        if (!std::isfinite(distance_meters) || distance_meters < 0.0 || distance_meters > static_cast<double>(max_distance_meters))
            return false;

        output.address = actor;
        output.name = actor_name;
        output.world_position = transform.Translation;
        output.distance_meters = static_cast<float>(distance_meters);
        output.kind = match.kind;
        strncpy_s(output.label, match.label, _TRUNCATE);
        return true;
    }

} // namespace wdgs::world_actors
