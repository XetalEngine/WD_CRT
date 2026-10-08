#pragma once
#include "config.h"
#include "game.h"

namespace extras
{
    void reset();
    void begin(std::uintptr_t world, const Config& settings);
    void collect(std::uintptr_t actor, const CameraIPC& camera, const Config& settings, game::Snapshot& output);
    void finish(const CameraIPC& camera, const Config& settings, game::Snapshot& output);
    void recoil(const wdgs::weapon_stats::Snapshot& weapon, bool enabled);
    bool vehicle_team(std::uintptr_t actor, void* local_faction, bool& teammate);
    bool derives(std::uintptr_t object, void* type);
} // namespace extras
