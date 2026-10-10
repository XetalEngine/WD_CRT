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
        bool team_known = true;
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
        FVector bone_world{}, aim_world{};
        std::uintptr_t actor_addr = 0;
        bool valid = false;
    };

    struct Snapshot
    {
        // False while the game is in a loading/menu world. The renderer must not
        // reuse the previous map snapshot during a world transition.
        bool valid = false;
        double time = 0;
        std::uintptr_t world = 0;
        std::uintptr_t controller = 0, pawn = 0, camera_manager = 0;
        std::vector<ProjectedPlayer> players;
        std::vector<ProjectedWorldActor> vehicles;
        std::vector<ProjectedWorldActor> dropped_items;
        CameraIPC camera{};
        float local_yaw = 0.f;
        bool local_yaw_valid = false;
        wchar_t local_name[65]{};
        PredictionLine prediction_line{};
        std::uintptr_t aim_selected_actor = 0;
        bool weapon_stats_valid = false;
        wdgs::weapon_stats::Snapshot weapon_stats{};
        wdgs::mortar_aim::Snapshot mortar{};
        wdgs::anti_sam::Status anti_sam{};
        struct Marker
        {
            std::uintptr_t actor = 0;
            FVector world{};
            char label[32]{};
            float distance = 0, fuse_left = 0, fuse_total = 0;
            int nearby = 0;
            bool bag = false, timed = false;
        };
        std::vector<Marker> markers;
        struct Trail
        {
            static constexpr int capacity = 12;
            FVector points[capacity]{};
            int count = 0;
            float hue = 0, alpha = 1;
        };
        std::vector<Trail> trails;
    };

    void init();
    void shutdown();
    void tick(Snapshot& snapshot, const Config& settings, bool menu_visible);
    bool camera_for_render(const Snapshot& snapshot, CameraIPC& camera);

} // namespace game
