#pragma once

#include "types.h"
#include <cstdint>

namespace wdgs::snapshot
{
    // Shared actor context lets additional snapshot modules (vehicles, loot, etc.)
    // use the same enumeration loop without duplicating local-player state.
    struct ActorSnapshotContext
    {
        std::int32_t player_name_id = 0;
        FVector observer_position{};
        void* local_faction = nullptr;
    };

    void* GetFaction(std::uintptr_t player_state);
    void begin_frame();
    void reset_caches();
    bool TryBuildPlayer(std::uintptr_t actor, const ActorSnapshotContext& context, Player& player, std::uintptr_t& root_component);
    void PopulatePlayerBones(std::uintptr_t actor, Player& player, bool full_skeleton, bool box);
    void PopulatePlayerMortarState(std::uintptr_t actor, std::uintptr_t root_component, Player& player);
} // namespace wdgs::snapshot
