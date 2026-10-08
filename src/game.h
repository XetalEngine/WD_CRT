#pragma once
#include "types.h"
#include "world_actor_snapshot.h"
#include "weapon_stats.h"
#include "mortar_aim.h"
#include "anti_sam.h"
#include <cstdint>
#include <vector>

struct Config;

namespace game
{

    struct ScreenPoint
    {
        float x = 0.f, y = 0.f;
        bool valid = false;
    };

    struct ProjectedPlayer
    {
        Player player;
        ScreenPoint screen;
        ScreenPoint bones[BONE_COUNT];
        std::uintptr_t actor_addr = 0;
        bool is_vehicle = false;
    };

    struct ProjectedWorldActor
    {
        wdgs::world_actors::Snapshot actor;
        ScreenPoint screen;
    };

    struct PredictionLine
    {
        ScreenPoint bone;
        ScreenPoint aim;
        std::uintptr_t actor_addr = 0;
        bool valid = false;
    };

    struct Snapshot
    {
        // False while the game is in a loading/menu world. The renderer must not
        // reuse the previous map snapshot during a world transition.
        bool valid = false;
        std::vector<ProjectedPlayer> players;
        std::vector<ProjectedWorldActor> vehicles;
        std::vector<ProjectedWorldActor> dropped_items;
        CameraIPC camera{};
        float local_yaw = 0.f;
        bool local_yaw_valid = false;
        PredictionLine prediction_line{};
        std::uintptr_t aim_selected_actor = 0;
        bool weapon_stats_valid = false;
        wdgs::weapon_stats::Snapshot weapon_stats{};
        wdgs::mortar_aim::Snapshot mortar{};
        wdgs::anti_sam::Status anti_sam{};
    };

    void init();
    void shutdown();
    void tick(Snapshot& snapshot, const Config& settings, bool menu_visible);

} // namespace game
