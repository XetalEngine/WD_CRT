#pragma once

#include "structs.h"
#include <cstdint>
#include <vector>

namespace wdgs::mortar_aim
{

    struct CandidateInfo
    {
        wchar_t name[32]{};
        float distance_m = 0.f;
        std::uintptr_t actor_addr = 0;
        bool in_range = true;
    };

    struct NativeProbeResult
    {
        float sight_units = 0.f;
        float initial_height_m = 0.f;
        float impact_distance = 0.f;
        bool ok = false;
    };

    struct NativeBallisticsDiagnostics
    {
        static constexpr int kProbes = 1;

        bool attempted = false;
        bool chain_ok = false;
        std::uint64_t capture_sequence = 0;
        std::uintptr_t extension = 0;
        std::uintptr_t weapon_component = 0;
        std::uintptr_t projectile_data = 0;
        std::uint32_t ammo_comparison_index = 0;
        std::uint32_t ammo_number = 0;
        char ammo_name[48]{};
        std::uint64_t projectile_stats_hash = 0;
        std::uint64_t weapon_stats_hash = 0;
        float projectile_length = 0.f;
        float projectile_diameter = 0.f;
        float projectile_mass = 0.f;
        float projectile_typical_speed = 0.f;
        float projectile_effective_range_multiplier = 0.f;
        float projectile_thrust_speed = 0.f;
        float weapon_typical_speed = 0.f;
        float captured_pitch_deg = 0.f;
        float captured_sight_units = 0.f;
        // Fallback only; capture_native_ballistics replaces this with
        // UWDWeatherSubsystem::GetAirDensity whenever the world is ready.
        float air_density = 1.225f;
        bool air_density_from_weather = false;
        bool native_table_ready = false;
        int native_table_rows = 0;
        float source_table_range_m = 0.f;
        float native_range_m = 0.f;
        float native_delta_m = 0.f;
        NativeProbeResult probes[kProbes]{};
        int probe_count = 0;
    };

    struct Snapshot
    {
        static constexpr int kSphArcPoints = 48;
        float range_m = 0.f;
        float sight_mils = 0.f;
        float pitch_deg = 0.f;
        float target_range_m = 0.f;
        float vehicle_pitch = 0.f;
        float vehicle_yaw = 0.f;
        float min_range_m = 0.f;
        float max_range_m = 0.f;
        bool valid = false;
        bool has_target = false;
        std::uintptr_t weapon_ptr = 0;
        FVector weapon_world_pos{};
        std::vector<CandidateInfo> candidates;
        std::uintptr_t selected_actor = 0;
        int selected_index = -1;

        // --- diagnostics (rendered by the debug overlay, SPH-2 only) ---
        bool dbg_is_dual = false; // dual-branch table = SPH-2 cannon
        int dbg_row_count = 0;
        int dbg_peak_index = 0;
        int dbg_artillery_ext = 0; // how many artillery data assets the weapon exposes
        float dbg_first_unit = 0.f;
        float dbg_first_dist = 0.f;
        float dbg_last_unit = 0.f;
        float dbg_last_dist = 0.f;
        float dbg_peak_unit = 0.f;
        float dbg_peak_dist = 0.f;
        float dbg_sight_units = 0.f;         // raw units derived from current pitch
        float dbg_target_units = 0.f;        // units the solver picked for the target
        float dbg_target_pitch = 0.f;        // resulting commanded pitch (deg)
        float dbg_pitch_error = 0.f;         // commanded local pitch minus current target pitch
        float dbg_applied_range_scale = 1.f; // branch-specific scale used by solver
        float dbg_target_dz_m = 0.f;         // target height relative to the gun
        float dbg_slant_dist_m = 0.f;        // 3D distance to target
        float dbg_cur_yaw = 0.f;             // current turret yaw (world)
        float dbg_target_yaw = 0.f;          // commanded turret yaw (local)
        float dbg_target_yaw_w = 0.f;        // absolute bearing to target (world)
        float dbg_flight_time = 0.f;         // projectile time of flight (s)
        float dbg_lead_m = 0.f;              // how far the aim point was led (m)
        float dbg_live_pitch = 0.f;          // where the barrel actually is
        float dbg_live_yaw = 0.f;
        float dbg_live_su = 0.f;  // sight units implied by the live pitch
        float dbg_live_rng = 0.f; // range the live pitch actually produces

        // Scene-component tree under the weapon: the barrel's elevation lives in a
        // child, not the root, so dump candidates to find which one pitches.
        static constexpr int kComps = 6;
        float dbg_comp_pitch[kComps]{};
        float dbg_comp_yaw[kComps]{};
        int dbg_comp_depth[kComps]{};
        FVector dbg_comp_world[kComps]{};
        FVector2D dbg_comp_screen[kComps]{};
        bool dbg_comp_screen_valid[kComps]{};
        bool dbg_comp_socket[kComps]{};
        char dbg_comp_name[kComps][64]{};
        int dbg_comp_count = 0;
        static constexpr int kSockets = 16;
        FVector dbg_socket_world[kSockets]{};
        FVector2D dbg_socket_screen[kSockets]{};
        bool dbg_socket_screen_valid[kSockets]{};
        char dbg_socket_name[kSockets][64]{};
        int dbg_socket_count = 0;
        float dbg_veh_pitch = 0.f;
        float dbg_veh_yaw = 0.f;

        // Black-box samples from WDArtilleryRangeLibrary.SimulateImpactDistance.
        // Captured at a bounded rate and frozen in the snapshot so the values are
        // readable in the existing vehicle diagnostics overlay.
        NativeBallisticsDiagnostics native{};

        // Raw table samples: unit_raw, distance, and the 3rd float of each stride
        // (currently unused by the solver — dumped to verify the record layout).
        static constexpr int kSamples = 6;
        float dbg_sample_u[kSamples]{};
        float dbg_sample_d[kSamples]{};
        float dbg_sample_x[kSamples]{};
        int dbg_sample_i[kSamples]{};
        int dbg_sample_count = 0;

        // SPH-2-only debug geometry.  The arc is kept in world space so the
        // renderer can project it every frame while the camera moves.  Numpad-9
        // copies the current landing point into the persistent marker fields.
        bool sph_debug_valid = false;
        bool sph_marker_locked = false;
        float sph_debug_range_m = 0.f;
        float sph_debug_scale = 1.f;
        FVector sph_arc_world[kSphArcPoints]{};
        int sph_arc_world_count = 0;
        FVector sph_landing_world{};
        FVector sph_marker_world{};
        bool sph_origin_from_socket = false;
        FVector2D sph_arc_screen[kSphArcPoints]{};
        bool sph_arc_screen_valid[kSphArcPoints]{};
        int sph_arc_screen_count = 0;
        FVector2D sph_origin_screen{};
        bool sph_origin_screen_valid = false;
        FVector2D sph_landing_screen{};
        bool sph_landing_screen_valid = false;
        FVector2D sph_marker_screen{};
        bool sph_marker_screen_valid = false;
        float sph_marker_screen_radius = 0.f;
    };

    struct TargetPlayer
    {
        FVector position;
        FVector velocity; // cm/s — needed to lead long time-of-flight shots
        std::uintptr_t actor_addr;
        bool is_team;
        float health;
        wchar_t name[32]{};
    };

    void reset();
    bool try_read(std::uintptr_t pawn, std::uintptr_t camera_manager, Snapshot& output);

    void auto_target(Snapshot& snap, const std::vector<TargetPlayer>& targets, bool key_down, bool team_check, int smooth, float fov_deg, float range_scale, int arc_mode, bool menu_visible);

    // Returns true if `vehicle` has an artillery/mortar weapon (used by ESP tagging).
    bool vehicle_is_mortar(std::uintptr_t vehicle);

} // namespace wdgs::mortar_aim
