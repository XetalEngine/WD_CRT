#include "stdafx.h"
#include "mortar_aim.h"
#include "offsets.h"
#include "engine_funcs.h"

#include <cmath>
#include <algorithm>
#include <vector>

namespace
{

    constexpr double kPi = 3.14159265358979323846;
    constexpr float kBarrelOffset = 2.f;
    // SPH-2 native probing/trajectory solving is disabled.  The repeated native
    // ProcessEvent/stat-buffer work was the source of the frame hitch; the normal
    // L81 mortar path remains available below.
    constexpr bool kEnableSph2Ballistics = false;
    // The native range is the field distance returned by the game simulation.
    // Keep calibration automatic and do not expose a user range-scale knob.
    // SimulateImpactDistance and the SPH-2 native table already return field
    // metres.  Do not apply a fixed compensation: the observed error changes
    // with elevation, so one global multiplier makes one angle correct and the
    // next angle miss.  The native per-angle probe is the sole scale (1.0).
    constexpr float kAutomaticSph2RangeScale = 1.0f;

    float automatic_sph2_range_scale(bool dual_mode)
    {
        return dual_mode ? kAutomaticSph2RangeScale : 1.f;
    }

    void build_sph_debug_arc(wdgs::mortar_aim::Snapshot& snap, float sight_units, float world_yaw_deg, float delta_z_m, float nominal_range_m, float range_scale)
    {
        snap.sph_debug_valid = false;
        snap.sph_arc_world_count = 0;
        snap.sph_debug_range_m = 0.f;
        if (!std::isfinite(sight_units) || !std::isfinite(world_yaw_deg) || !std::isfinite(nominal_range_m) || nominal_range_m <= 1.f || !std::isfinite(range_scale) || range_scale <= 0.f)
            return;

        const double theta = static_cast<double>(sight_units) / 1000.0 +
                             static_cast<double>(kBarrelOffset) * kPi / 180.0;
        const double range_m = static_cast<double>(nominal_range_m) * range_scale;
        const double yaw = static_cast<double>(world_yaw_deg) * kPi / 180.0;
        const double ux = std::cos(yaw);
        const double uy = std::sin(yaw);
        const double tan_theta = std::tan(theta);
        if (!std::isfinite(range_m) || range_m <= 1.0 || !std::isfinite(tan_theta))
            return;

        const FVector origin = snap.weapon_world_pos;
        snap.sph_debug_scale = range_scale;
        snap.sph_debug_range_m = static_cast<float>(range_m);
        snap.sph_landing_world = {
            origin.X + ux * range_m * 100.0,
            origin.Y + uy * range_m * 100.0,
            origin.Z + static_cast<double>(delta_z_m) * 100.0};

        constexpr int kPoints = wdgs::mortar_aim::Snapshot::kSphArcPoints;
        for (int i = 0; i < kPoints; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(kPoints - 1);
            const double x_m = range_m * t;
            // A clear, deterministic ballistic arc.  The endpoint is the field
            // landing plane; the table/native distance controls its length.
            const double z_m = range_m * tan_theta * t * (1.0 - t) +
                               static_cast<double>(delta_z_m) * t;
            snap.sph_arc_world[i] = {
                origin.X + ux * x_m * 100.0,
                origin.Y + uy * x_m * 100.0,
                origin.Z + z_m * 100.0};
        }
        snap.sph_arc_world_count = kPoints;
        snap.sph_debug_valid = true;
    }
    // Range table
    struct RangeRow
    {
        float unit_raw;
        float distance_m;
        float flight_time_s; // 3rd float of the stride: projectile time of flight
    };

    struct CachedTable
    {
        std::uintptr_t data_asset = 0;
        std::vector<RangeRow> rows;
        std::size_t peak_index = 0; // index of max-range row; splits direct/indirect
    };

    static CachedTable g_cached_table;
    static CachedTable g_native_table;
    static wdgs::mortar_aim::NativeBallisticsDiagnostics g_native_diag;
    static ULONGLONG g_native_last_capture_ms = 0;
    static std::uintptr_t g_native_last_extension = 0;
    static std::uint64_t g_native_stats_key = 0;
    static engine_funcs::ProjectileStatsBuffer g_native_projectile_stats{};
    static engine_funcs::WeaponStatsBuffer g_native_weapon_stats{};
    static float g_native_air_density = 1.225f;
    static bool g_native_inputs_ready = false;
    bool prev_pgup = false, prev_pgdn = false;
    std::uintptr_t s_selected_actor = 0;
    int s_selected_index = 0;

    std::uint64_t hash_bytes(const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::uint64_t hash = 1469598103934665603ull;
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    float float_at(const std::uint8_t* data, std::size_t size, std::size_t offset)
    {
        float value = 0.f;
        if (data && offset + sizeof(value) <= size)
            std::memcpy(&value, data + offset, sizeof(value));
        return std::isfinite(value) ? value : 0.f;
    }

    bool native_impact_distance(float sight_units, float initial_height_m, float& distance_m)
    {
        if (!g_native_inputs_ready || !std::isfinite(sight_units) || !std::isfinite(initial_height_m))
            return false;
        return engine_funcs::simulate_impact_distance(g_native_projectile_stats, g_native_weapon_stats, sight_units, initial_height_m, g_native_air_density, distance_m) &&
               std::isfinite(distance_m) && distance_m > 0.f;
    }

    float solve_native_height_units(float horizontal_m, float target_delta_z_m, std::size_t start, std::size_t end, float fallback_units)
    {
        if (!g_native_inputs_ready || g_native_table.rows.empty() || start >= g_native_table.rows.size() || end >= g_native_table.rows.size() || start >= end || !std::isfinite(horizontal_m) || horizontal_m <= 0.f)
            return fallback_units;

        // SimulateImpactDistance defines InitialHeightMeters relative to the
        // impact plane. A target above the muzzle therefore makes the initial
        // height negative; a target below it makes it positive.
        const float initial_height_m = -target_delta_z_m;
        float d0 = 0.f, d1 = 0.f;
        if (!native_impact_distance(g_native_table.rows[start].unit_raw, initial_height_m, d0) || !native_impact_distance(g_native_table.rows[end].unit_raw, initial_height_m, d1))
            return fallback_units;

        const bool descending = d0 > d1;
        const float branch_min = (std::min)(d0, d1);
        const float branch_max = (std::max)(d0, d1);
        if (horizontal_m < branch_min || horizontal_m > branch_max)
            return fallback_units;

        float lo = g_native_table.rows[start].unit_raw;
        float hi = g_native_table.rows[end].unit_raw;
        for (int i = 0; i < 36; ++i)
        {
            const float mid = (lo + hi) * 0.5f;
            float dm = 0.f;
            if (!native_impact_distance(mid, initial_height_m, dm))
                return fallback_units;
            const bool move_lo = descending ? (dm > horizontal_m)
                                            : (dm < horizontal_m);
            if (move_lo)
                lo = mid;
            else
                hi = mid;
        }
        const float solved = (lo + hi) * 0.5f;
        return std::isfinite(solved) ? solved : fallback_units;
    }

    bool build_range_table(std::uintptr_t data_asset, CachedTable& out)
    {
        out = {};
        out.data_asset = data_asset;

        TArray<std::uint8_t> raw = read<TArray<std::uint8_t>>(data_asset + offsets::UWDVehicleWeaponData::ArtilleryRangeData);
        if (raw.Num() <= 0 || raw.Num() > 10000)
            return false;

        constexpr int kStride = 12;
        const int entry_count = raw.Num();
        const auto base = reinterpret_cast<std::uintptr_t>(raw.Data);

        float max_dist = 0.f;
        std::vector<RangeRow> rows;
        rows.reserve(entry_count);

        for (int i = 0; i < entry_count; i++)
        {
            const std::uintptr_t addr = base + static_cast<std::size_t>(i) * kStride;
            float unit_raw = 0.f, impact_dist = 0.f, tof = 0.f;
            if (!read_mem(addr + 0, &unit_raw, sizeof(float)) || !read_mem(addr + 4, &impact_dist, sizeof(float)))
                continue;
            read_mem(addr + 8, &tof, sizeof(float));
            if (!std::isfinite(unit_raw) || !std::isfinite(impact_dist))
                continue;
            if (!std::isfinite(tof) || tof < 0.f)
                tof = 0.f;
            if (std::fabs(impact_dist) > max_dist)
                max_dist = std::fabs(impact_dist);
            rows.push_back({unit_raw, impact_dist, tof});
        }

        if (rows.empty())
            return false;

        if (max_dist >= 5000.f)
        {
            for (auto& r : rows)
                r.distance_m /= 100.f;
        }

        std::sort(rows.begin(), rows.end(), [](const RangeRow& a, const RangeRow& b)
                  { return a.unit_raw < b.unit_raw; });

        // Find the peak — the row with maximum distance. This separates direct fire
        // (left of peak, small pitch) from indirect fire (right of peak, large pitch).
        std::size_t peak = 0;
        float peak_dist = rows[0].distance_m;
        for (std::size_t i = 1; i < rows.size(); i++)
        {
            if (rows[i].distance_m > peak_dist)
            {
                peak_dist = rows[i].distance_m;
                peak = i;
            }
        }
        out.peak_index = peak;
        out.rows = std::move(rows);
        return true;
    }

    float interpolate_range(const std::vector<RangeRow>& rows, float sight_units)
    {
        if (rows.empty())
            return 0.f;
        if (sight_units <= rows.front().unit_raw)
            return rows.front().distance_m;
        if (sight_units >= rows.back().unit_raw)
            return rows.back().distance_m;

        for (size_t i = 1; i < rows.size(); i++)
        {
            if (sight_units <= rows[i].unit_raw)
            {
                float t = (sight_units - rows[i - 1].unit_raw) /
                          (rows[i].unit_raw - rows[i - 1].unit_raw);
                return rows[i - 1].distance_m + t * (rows[i].distance_m - rows[i - 1].distance_m);
            }
        }
        return rows.back().distance_m;
    }

    // Interpolate over a subrange of rows [start..end] (inclusive).
    float inverse_interpolate_range(const std::vector<RangeRow>& rows, float dist_m, std::size_t start, std::size_t end)
    {
        if (rows.empty() || start >= rows.size() || end >= rows.size() || start > end)
            return 0.f;

        bool descending = rows[start].distance_m > rows[end].distance_m;

        for (std::size_t i = start; i + 1 <= end; i++)
        {
            float d0 = rows[i].distance_m;
            float d1 = rows[i + 1].distance_m;
            bool between = descending
                               ? (dist_m <= d0 && dist_m >= d1)
                               : (dist_m >= d0 && dist_m <= d1);

            if (between)
            {
                float span = d1 - d0;
                if (std::fabs(span) < 1e-3f)
                    return rows[i].unit_raw;
                float t = (dist_m - d0) / span;
                return rows[i].unit_raw + t * (rows[i + 1].unit_raw - rows[i].unit_raw);
            }
        }

        // Out of range on this branch — clamp to the physically nearest endpoint.
        // The previous max-distance test returned the PEAK for a value below the
        // minimum of a descending (indirect) branch.  Example: 625 m on the
        // 2600->700 m branch incorrectly selected 619 mil/2600 m instead of the
        // 1400 mil/700 m endpoint.
        float d_start = rows[start].distance_m;
        float d_end = rows[end].distance_m;
        return std::fabs(dist_m - d_start) <= std::fabs(dist_m - d_end)
                   ? rows[start].unit_raw
                   : rows[end].unit_raw;
    }

    // Legacy full-table interpolation (used when there's no peak split).
    float inverse_interpolate(const std::vector<RangeRow>& rows, float dist_m)
    {
        return inverse_interpolate_range(rows, dist_m, 0, rows.size() - 1);
    }

    // The artillery table describes the impact range on level ground.  Rebuild the
    // vertical coordinate of that trajectory at a requested horizontal distance
    // using the table range itself as the ballistic constraint:
    //
    //     z(x) = x * tan(theta) * (1 - x / level_range(theta))
    //
    // This is the exact no-drag trajectory after eliminating muzzle velocity and
    // gravity through the level-ground range.  Using the measured in-game range
    // curve keeps the correction tied to the actual weapon table.  On the direct
    // branch z grows with sight units; on the indirect branch it falls, so the same
    // root finder naturally applies the opposite height-correction sign.
    float trajectory_height_at_range(const std::vector<RangeRow>& rows, float sight_units, float horizontal_m, float range_scale)
    {
        const float raw_level_range = interpolate_range(rows, sight_units);
        if (!std::isfinite(raw_level_range) || raw_level_range <= 0.f || !std::isfinite(horizontal_m) || horizontal_m <= 0.f || !std::isfinite(range_scale) || range_scale <= 0.f)
            return 0.f;

        // The SPH field calibration is actual_world_range / nominal_native_range.
        // Therefore a native/table row reaches raw * scale metres in the world.
        const float level_range_m = raw_level_range * range_scale;
        const double theta = static_cast<double>(sight_units) / 1000.0 +
                             static_cast<double>(kBarrelOffset) * kPi / 180.0;
        const double height = static_cast<double>(horizontal_m) * std::tan(theta) *
                              (1.0 - static_cast<double>(horizontal_m) / static_cast<double>(level_range_m));
        return std::isfinite(height) ? static_cast<float>(height) : 0.f;
    }

    float solve_height_corrected_units(const std::vector<RangeRow>& rows, float horizontal_m, float delta_z_m, float range_scale, std::size_t start, std::size_t end)
    {
        const float safe_scale = std::isfinite(range_scale) && range_scale > 0.f
                                     ? range_scale
                                     : 1.f;
        // Convert the requested world distance back to the native table distance.
        // The automatic scale is unity because the live simulation already reports
        // metres in the same field coordinate system.
        const float flat_units = inverse_interpolate_range(rows, horizontal_m / safe_scale, start, end);

        if (rows.empty() || start >= rows.size() || end >= rows.size() || start >= end || !std::isfinite(delta_z_m) || std::fabs(delta_z_m) < 0.01f)
            return flat_units;

        float lo = rows[start].unit_raw;
        float hi = rows[end].unit_raw;
        float z_lo = trajectory_height_at_range(rows, lo, horizontal_m, safe_scale);
        float z_hi = trajectory_height_at_range(rows, hi, horizontal_m, safe_scale);

        if (!std::isfinite(z_lo) || !std::isfinite(z_hi))
            return flat_units;

        const float z_min = (std::min)(z_lo, z_hi);
        const float z_max = (std::max)(z_lo, z_hi);
        if (delta_z_m < z_min || delta_z_m > z_max)
            return flat_units;

        const bool increasing = z_hi > z_lo;
        for (int i = 0; i < 32; ++i)
        {
            const float mid = (lo + hi) * 0.5f;
            const float z_mid = trajectory_height_at_range(rows, mid, horizontal_m, safe_scale);

            if ((increasing && z_mid < delta_z_m) || (!increasing && z_mid > delta_z_m))
            {
                lo = mid;
            }
            else
            {
                hi = mid;
            }
        }

        const float corrected = (lo + hi) * 0.5f;
        return std::isfinite(corrected) ? corrected : flat_units;
    }

    // Time of flight for a given sight unit. Rows are sorted by unit_raw, and
    // flight time rises monotonically with elevation, so a plain scan works.
    float interpolate_flight_time(const std::vector<RangeRow>& rows, float sight_units)
    {
        if (rows.empty())
            return 0.f;
        if (sight_units <= rows.front().unit_raw)
            return rows.front().flight_time_s;
        if (sight_units >= rows.back().unit_raw)
            return rows.back().flight_time_s;

        for (std::size_t i = 1; i < rows.size(); i++)
        {
            if (sight_units <= rows[i].unit_raw)
            {
                float span = rows[i].unit_raw - rows[i - 1].unit_raw;
                if (std::fabs(span) < 1e-3f)
                    return rows[i].flight_time_s;
                float t = (sight_units - rows[i - 1].unit_raw) / span;
                return rows[i - 1].flight_time_s +
                       t * (rows[i].flight_time_s - rows[i - 1].flight_time_s);
            }
        }
        return rows.back().flight_time_s;
    }
    // Weapon discovery
    bool is_artillery_data(std::uintptr_t data_asset)
    {
        if (!is_valid_ptr(reinterpret_cast<void*>(data_asset)))
            return false;
        return read<std::uint8_t>(data_asset + offsets::UWDVehicleWeaponData::WeaponType) == 2;
    }

    std::uintptr_t try_weapon_from_extensions(std::uintptr_t actor)
    {
        TArray<std::uintptr_t> ext = read<TArray<std::uintptr_t>>(actor + offsets::AWDVehicleWeapon::WeaponExtensions);
        if (!ext.IsSane(16) || ext.Num() <= 0)
            return 0;

        for (int e = 0; e < ext.Num(); e++)
        {
            std::uintptr_t ex = ext[e];
            if (!is_valid_ptr(reinterpret_cast<void*>(ex)))
                continue;
            std::uintptr_t da = read<std::uintptr_t>(ex + offsets::UWDVehicleWeaponExtension::DataAsset);
            if (is_artillery_data(da))
                return actor;
        }
        return 0;
    }

    std::uintptr_t find_artillery_in_manager(std::uintptr_t vehicle)
    {
        constexpr std::uintptr_t kInstComps = 0x290;
        TArray<std::uintptr_t> inst_comps = read<TArray<std::uintptr_t>>(vehicle + kInstComps);
        if (!inst_comps.IsSane(256) || inst_comps.Num() <= 0)
            return 0;

        for (int i = 0; i < inst_comps.Num(); i++)
        {
            std::uintptr_t comp = inst_comps[i];
            if (!is_valid_ptr(reinterpret_cast<void*>(comp)))
                continue;

            TArray<std::uintptr_t> weapons = read<TArray<std::uintptr_t>>(comp + offsets::UWDWeaponManagerComponent::Weapons);
            if (!weapons.IsSane(32) || weapons.Num() <= 0)
                continue;

            for (int w = 0; w < weapons.Num(); w++)
            {
                std::uintptr_t wpn = try_weapon_from_extensions(weapons[w]);
                if (wpn)
                    return wpn;
            }
        }
        return 0;
    }

    std::uintptr_t find_artillery_in_hierarchy(std::uintptr_t vehicle)
    {
        std::uintptr_t root = read<std::uintptr_t>(vehicle + offsets::AActor::RootComponent);
        if (!is_valid_ptr(reinterpret_cast<void*>(root)))
            return 0;

        constexpr std::uintptr_t kAttachOffsets[] = {0x108, 0x100, 0x118};

        for (auto off : kAttachOffsets)
        {
            TArray<std::uintptr_t> attach = read<TArray<std::uintptr_t>>(root + off);
            if (!attach.IsSane(256) || attach.Num() <= 0)
                continue;

            for (int i = 0; i < attach.Num(); i++)
            {
                std::uintptr_t child = attach[i];
                if (!is_valid_ptr(reinterpret_cast<void*>(child)))
                    continue;

                std::uintptr_t owner = read<std::uintptr_t>(child + offsets::UObject::OuterPrivate);
                if (is_valid_ptr(reinterpret_cast<void*>(owner)) && owner != vehicle)
                {
                    std::uintptr_t wpn = find_artillery_in_manager(owner);
                    if (!wpn)
                        wpn = try_weapon_from_extensions(owner);
                    if (wpn)
                        return wpn;
                }

                TArray<std::uintptr_t> sub = read<TArray<std::uintptr_t>>(child + off);
                if (!sub.IsSane(256))
                    continue;
                for (int j = 0; j < sub.Num(); j++)
                {
                    std::uintptr_t gc = sub[j];
                    if (!is_valid_ptr(reinterpret_cast<void*>(gc)))
                        continue;
                    std::uintptr_t go = read<std::uintptr_t>(gc + offsets::UObject::OuterPrivate);
                    if (is_valid_ptr(reinterpret_cast<void*>(go)) && go != vehicle)
                    {
                        std::uintptr_t wpn = find_artillery_in_manager(go);
                        if (!wpn)
                            wpn = try_weapon_from_extensions(go);
                        if (wpn)
                            return wpn;
                    }
                }
            }
        }
        return 0;
    }

    std::uintptr_t find_weapon(std::uintptr_t vehicle, std::uintptr_t camera_manager, std::uintptr_t pawn)
    {
        std::uintptr_t weapon = find_artillery_in_manager(vehicle);

        if (!weapon)
            weapon = find_artillery_in_hierarchy(vehicle);

        if (!weapon && is_valid_ptr(reinterpret_cast<void*>(camera_manager)))
        {
            std::uintptr_t vt_target = read<std::uintptr_t>(camera_manager + offsets::APlayerCameraManager::ViewTarget + offsets::FTViewTarget::Target);
            if (is_valid_ptr(reinterpret_cast<void*>(vt_target)) && vt_target != pawn)
                weapon = try_weapon_from_extensions(vt_target);
        }

        return weapon;
    }

    std::uintptr_t find_data_asset(std::uintptr_t weapon)
    {
        TArray<std::uintptr_t> extensions = read<TArray<std::uintptr_t>>(weapon + offsets::AWDVehicleWeapon::WeaponExtensions);
        if (!extensions.IsSane(16) || extensions.Num() <= 0)
            return 0;

        for (int e = 0; e < extensions.Num(); e++)
        {
            std::uintptr_t ext = extensions[e];
            if (!is_valid_ptr(reinterpret_cast<void*>(ext)))
                continue;
            std::uintptr_t data = read<std::uintptr_t>(ext + offsets::UWDVehicleWeaponExtension::DataAsset);
            if (is_artillery_data(data))
                return data;
        }
        return 0;
    }

    // SPH-2 is the only artillery asset in the current build exposing the paired
    // turret barrel and muzzle-flash sockets.  Use that stable identity in
    // addition to the table-peak heuristic; some SPH-2 range tables are monotonic
    // after the native conversion and would otherwise be mistaken for the L81.
    bool has_sph2_socket_pair(std::uintptr_t weapon)
    {
        if (!is_valid_ptr(reinterpret_cast<void*>(weapon)))
            return false;
        const std::uintptr_t mesh = read<std::uintptr_t>(weapon + offsets::AWDVehicleWeapon::SkeletalMesh);
        if (!is_valid_ptr(reinterpret_cast<void*>(mesh)))
            return false;
        const TArray<FNameValue> names = engine_funcs::get_all_socket_names(reinterpret_cast<void*>(mesh));
        if (!names.IsSane(64))
            return false;
        bool muzzle = false, barrel = false;
        for (int i = 0; i < names.Num(); ++i)
        {
            if (names[i].ComparisonIndex == 0)
                continue;
            FString text = engine_funcs::conv_name_to_string(names[i]);
            if (!text.IsValid())
                continue;
            const std::wstring wide = text.ToWString(63);
            std::string lower;
            lower.reserve(wide.size());
            for (wchar_t c : wide)
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            muzzle = muzzle || lower == "muzzleflash_01_socket";
            barrel = barrel || lower == "turretbarrel_01_attach";
            engine_funcs::release_string(text);
        }
        return muzzle && barrel;
    }

    std::uintptr_t find_artillery_extension(std::uintptr_t weapon)
    {
        TArray<std::uintptr_t> extensions = read<TArray<std::uintptr_t>>(weapon + offsets::AWDVehicleWeapon::WeaponExtensions);
        if (!extensions.IsSane(16) || extensions.Num() <= 0)
            return 0;

        for (int e = 0; e < extensions.Num(); ++e)
        {
            const std::uintptr_t extension = extensions[e];
            if (!is_valid_ptr(reinterpret_cast<void*>(extension)))
                continue;
            const std::uintptr_t data_asset = read<std::uintptr_t>(extension + offsets::UWDVehicleWeaponExtension::DataAsset);
            if (is_artillery_data(data_asset))
                return extension;
        }
        return 0;
    }

    void capture_native_ballistics(std::uintptr_t extension, float pitch_deg, float sight_units, wdgs::mortar_aim::NativeBallisticsDiagnostics& out)
    {
        using namespace wdgs::mortar_aim;
        const std::uint64_t next_sequence = out.capture_sequence + 1;
        out = {};
        out.attempted = true;
        out.capture_sequence = next_sequence;
        out.extension = extension;
        out.captured_pitch_deg = pitch_deg;
        out.captured_sight_units = sight_units;
        out.air_density = 1.225f;
        g_native_inputs_ready = false;

        if (!is_valid_ptr(reinterpret_cast<void*>(extension)))
            return;

        // The default is only a deterministic fallback for frames where the
        // weather subsystem is not available yet.  In a live world use the same
        // UWDWeatherSubsystem value consumed by the native ballistic function.
        float weather_air_density = 0.f;
        if (engine_funcs::get_air_density(reinterpret_cast<void*>(extension), weather_air_density))
        {
            out.air_density = weather_air_density;
            out.air_density_from_weather = true;
        }

        engine_funcs::ProjectileStatsBuffer projectile_stats{};
        engine_funcs::WeaponStatsBuffer weapon_stats{};
        FNameValue ammo_modifier{};

        void* weapon_component = engine_funcs::get_weapon_component(reinterpret_cast<void*>(extension));
        out.weapon_component = reinterpret_cast<std::uintptr_t>(weapon_component);
        if (!weapon_component)
            return;

        void* projectile_data = engine_funcs::get_projectile_data(reinterpret_cast<void*>(extension), weapon_component, ammo_modifier);
        out.projectile_data = reinterpret_cast<std::uintptr_t>(projectile_data);
        out.ammo_comparison_index = ammo_modifier.ComparisonIndex;
        out.ammo_number = ammo_modifier.Number;
        if (!projectile_data)
            return;

        FString ammo_string = engine_funcs::conv_name_to_string(ammo_modifier);
        if (ammo_string.IsValid())
        {
            const std::wstring wide = ammo_string.ToWString(47);
            if (!wide.empty())
            {
                WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.ammo_name, static_cast<int>(sizeof(out.ammo_name)), nullptr, nullptr);
            }
            engine_funcs::release_string(ammo_string);
        }

        const bool projectile_ok = engine_funcs::get_projectile_stats(projectile_data, ammo_modifier, projectile_stats);
        const bool weapon_ok = engine_funcs::get_weapon_stats(weapon_component, weapon_stats);
        if (!projectile_ok || !weapon_ok)
            return;

        // Keep the exact opaque inputs used by SimulateImpactDistance available
        // to the height-aware solver. This avoids replacing the native gravity,
        // drag, mass, diameter, and muzzle-speed model with a no-drag shortcut.
        g_native_projectile_stats = projectile_stats;
        g_native_weapon_stats = weapon_stats;
        g_native_air_density = out.air_density;
        g_native_inputs_ready = true;

        out.chain_ok = true;
        out.projectile_stats_hash = hash_bytes(projectile_stats.Data, sizeof(projectile_stats.Data));
        out.weapon_stats_hash = hash_bytes(weapon_stats.Data, sizeof(weapon_stats.Data));
        // Named offsets from the generated SDK layouts. Keeping these reads beside
        // the opaque buffers makes the probe useful without duplicating the full
        // game structs in production code.
        out.projectile_length = float_at(projectile_stats.Data, sizeof(projectile_stats.Data), 0x68);
        out.projectile_diameter = float_at(projectile_stats.Data, sizeof(projectile_stats.Data), 0x6C);
        out.projectile_mass = float_at(projectile_stats.Data, sizeof(projectile_stats.Data), 0x70);
        out.projectile_typical_speed = float_at(projectile_stats.Data, sizeof(projectile_stats.Data), 0x1FC);
        out.projectile_effective_range_multiplier = float_at(projectile_stats.Data, sizeof(projectile_stats.Data), 0x200);
        out.projectile_thrust_speed = float_at(projectile_stats.Data, sizeof(projectile_stats.Data), 0x2B0);
        out.weapon_typical_speed = float_at(weapon_stats.Data, sizeof(weapon_stats.Data), 0xA4);

        // The native implementation was recovered from the current executable:
        //   launch_angle_rad = sight_units * 0.001f + 0.016808f
        //   vertical_position = initial_height_m
        //   integration step = 0.01 s (internal, not a reflected parameter)
        // Its return value is already horizontal impact distance in metres.
        const std::uint64_t stats_key = out.projectile_stats_hash ^
                                        (out.weapon_stats_hash + 0x9E3779B97F4A7C15ull + (out.projectile_stats_hash << 6) + (out.projectile_stats_hash >> 2));
        const bool rebuild_native_table =
            stats_key != g_native_stats_key ||
            g_native_table.data_asset != g_cached_table.data_asset ||
            g_native_table.rows.size() != g_cached_table.rows.size();

        if (rebuild_native_table)
        {
            CachedTable rebuilt{};
            rebuilt.data_asset = g_cached_table.data_asset;
            rebuilt.rows.reserve(g_cached_table.rows.size());

            bool complete = !g_cached_table.rows.empty();
            for (const auto& source : g_cached_table.rows)
            {
                float native_range_m = 0.f;
                if (!engine_funcs::simulate_impact_distance(projectile_stats, weapon_stats, source.unit_raw, 0.f, out.air_density, native_range_m) || !std::isfinite(native_range_m) || native_range_m <= 0.f)
                {
                    complete = false;
                    break;
                }
                rebuilt.rows.push_back({source.unit_raw, native_range_m,
                                        source.flight_time_s});
            }

            if (complete && rebuilt.rows.size() == g_cached_table.rows.size())
            {
                rebuilt.peak_index = 0;
                for (std::size_t i = 1; i < rebuilt.rows.size(); ++i)
                {
                    if (rebuilt.rows[i].distance_m > rebuilt.rows[rebuilt.peak_index].distance_m)
                        rebuilt.peak_index = i;
                }
                g_native_table = std::move(rebuilt);
                g_native_stats_key = stats_key;
            }
            else
            {
                g_native_table = {};
                g_native_stats_key = 0;
            }
        }

        out.native_table_ready = !g_native_table.rows.empty() &&
                                 g_native_table.rows.size() == g_cached_table.rows.size();
        out.native_table_rows = static_cast<int>(g_native_table.rows.size());
        out.source_table_range_m = interpolate_range(g_cached_table.rows, sight_units);
        if (out.native_table_ready)
        {
            out.native_range_m = interpolate_range(g_native_table.rows, sight_units);
            out.native_delta_m = out.native_range_m - out.source_table_range_m;
        }

        out.probe_count = 1;
        auto& probe = out.probes[0];
        probe.sight_units = sight_units;
        probe.initial_height_m = 0.f;
        probe.ok = engine_funcs::simulate_impact_distance(projectile_stats, weapon_stats, probe.sight_units, probe.initial_height_m, out.air_density, probe.impact_distance);
    }
    // Vehicle attitude from quaternion
    struct VehicleAttitude
    {
        float pitch = 0.f;
        float yaw = 0.f;
        bool valid = false;
    };

    VehicleAttitude read_vehicle_attitude(std::uintptr_t vehicle)
    {
        VehicleAttitude att{};
        std::uintptr_t root = read<std::uintptr_t>(vehicle + offsets::AActor::RootComponent);
        if (!is_valid_ptr(reinterpret_cast<void*>(root)))
            return att;

        struct
        {
            double X, Y, Z, W;
        } quat = read<decltype(quat)>(root + offsets::USceneComponent::ComponentToWorld);

        double sinp = 2.0 * (quat.W * quat.Y - quat.Z * quat.X);
        sinp = std::clamp(sinp, -1.0, 1.0);
        att.pitch = static_cast<float>(std::asin(sinp) * 180.0 / kPi);

        double siny = 2.0 * (quat.W * quat.Z + quat.X * quat.Y);
        double cosy = 1.0 - 2.0 * (quat.Y * quat.Y + quat.Z * quat.Z);
        att.yaw = static_cast<float>(std::atan2(siny, cosy) * 180.0 / kPi);

        att.valid = std::isfinite(att.pitch) && std::isfinite(att.yaw);
        return att;
    }

    VehicleAttitude read_component_attitude(std::uintptr_t component)
    {
        VehicleAttitude att{};
        if (!is_valid_ptr(reinterpret_cast<void*>(component)))
            return att;
        struct
        {
            double X, Y, Z, W;
        } quat = read<decltype(quat)>(component + offsets::USceneComponent::ComponentToWorld);
        double n = quat.X * quat.X + quat.Y * quat.Y +
                   quat.Z * quat.Z + quat.W * quat.W;
        if (!std::isfinite(n) || n < 0.5 || n > 1.5)
            return att;
        double sinp = std::clamp(2.0 * (quat.W * quat.Y - quat.Z * quat.X), -1.0, 1.0);
        att.pitch = static_cast<float>(std::asin(sinp) * 180.0 / kPi);
        double siny = 2.0 * (quat.W * quat.Z + quat.X * quat.Y);
        double cosy = 1.0 - 2.0 * (quat.Y * quat.Y + quat.Z * quat.Z);
        att.yaw = static_cast<float>(std::atan2(siny, cosy) * 180.0 / kPi);
        att.valid = std::isfinite(att.pitch) && std::isfinite(att.yaw);
        return att;
    }

} // namespace
// Public API
bool wdgs::mortar_aim::vehicle_is_mortar(std::uintptr_t vehicle)
{
    if (!is_valid_ptr(reinterpret_cast<void*>(vehicle)))
        return false;
    std::uintptr_t weapon = find_artillery_in_manager(vehicle);
    if (!weapon)
        weapon = find_artillery_in_hierarchy(vehicle);
    return weapon != 0;
}

void wdgs::mortar_aim::reset()
{
    g_cached_table = {};
    g_native_table = {};
    g_native_diag = {};
    g_native_last_capture_ms = 0;
    g_native_last_extension = 0;
    g_native_stats_key = 0;
    g_native_projectile_stats = {};
    g_native_weapon_stats = {};
    g_native_air_density = 1.225f;
    g_native_inputs_ready = false;
    prev_pgup = prev_pgdn = false;
    s_selected_actor = 0;
    s_selected_index = 0;
}

bool wdgs::mortar_aim::try_read(std::uintptr_t pawn, std::uintptr_t camera_manager, Snapshot& output)
{
    output = {};

    if (!is_valid_ptr(reinterpret_cast<void*>(pawn)))
        return false;

    std::uintptr_t op = read<std::uintptr_t>(pawn + offsets::AWDMoverCharacter::VehicleOperator);
    if (!is_valid_ptr(reinterpret_cast<void*>(op)))
        return false;

    std::uintptr_t seat = read<std::uintptr_t>(op + offsets::UWDVehicleOperatorComponent::CurrentSeat);
    if (!is_valid_ptr(reinterpret_cast<void*>(seat)))
        seat = read<std::uintptr_t>(op + offsets::UWDVehicleOperatorComponent::ReplicatedSeat);
    if (!is_valid_ptr(reinterpret_cast<void*>(seat)))
        return false;

    std::uintptr_t vehicle = read<std::uintptr_t>(seat + offsets::UObject::OuterPrivate);
    if (!is_valid_ptr(reinterpret_cast<void*>(vehicle)))
        return false;

    std::uintptr_t weapon = find_weapon(vehicle, camera_manager, pawn);
    if (!weapon)
        return false;

    std::uintptr_t data_asset = find_data_asset(weapon);
    if (!data_asset)
        return false;

    if (g_cached_table.data_asset != data_asset || g_cached_table.rows.empty())
    {
        if (!build_range_table(data_asset, g_cached_table))
            return false;
    }

    std::uintptr_t rot_comp = read<std::uintptr_t>(weapon + offsets::AWDVehicleWeapon::RotationComponent);
    if (!is_valid_ptr(reinterpret_cast<void*>(rot_comp)))
        return false;

    FRotator target_rot = read<FRotator>(rot_comp + offsets::UWDWeaponRotationComponent::TargetRotation);

    std::uintptr_t owning_vehicle = read<std::uintptr_t>(weapon + offsets::AWDVehicleWeapon::OwningVehicle);

    VehicleAttitude veh_att{};
    if (is_valid_ptr(reinterpret_cast<void*>(owning_vehicle)))
        veh_att = read_vehicle_attitude(owning_vehicle);

    output.vehicle_pitch = veh_att.pitch;
    output.vehicle_yaw = veh_att.yaw;
    output.dbg_cur_yaw = static_cast<float>(target_rot.Yaw + veh_att.yaw);

    float pitch_deg = target_rot.Pitch + veh_att.pitch;
    if (!std::isfinite(pitch_deg))
        return false;

    float sight_units = static_cast<float>((static_cast<double>(pitch_deg) - kBarrelOffset) * 1000.0 * kPi / 180.0);

    const float source_range_m = interpolate_range(g_cached_table.rows, sight_units);
    if (!std::isfinite(source_range_m) || source_range_m < 0.f)
        return false;

    output.pitch_deg = pitch_deg;
    // The table's unit_raw IS the sight mil value — verified against the game's
    // own MIL/RNG ruler (1340 mil lines up with 994 m, table gives 995.9 m).
    output.sight_mils = sight_units;
    output.range_m = source_range_m;
    output.weapon_ptr = weapon;
    output.valid = true;

    // The SPH-2 and the hand L81 mortar expose different calibration models.
    // Only the SPH-2 table has a direct/indirect peak in the middle.  The
    // regular mortar used to consume the reflected ArtilleryRangeData curve
    // directly; keep that path intact and never replace it with the native
    // probe (the native probe is the SPH-2-specific telemetry path).
    const bool dual_mode = g_cached_table.peak_index > 0 &&
                           g_cached_table.peak_index < g_cached_table.rows.size() - 1;

    if (dual_mode && !kEnableSph2Ballistics)
    {
        // Do not publish a SPH-2 snapshot.  This prevents native probing,
        // marker projection, arc generation and SPH-2 aiming from running at
        // all, while leaving the regular mortar calibration untouched.
        output = {};
        return false;
    }

    // Keep a stable black-box capture on screen. F8 forces an immediate sample;
    // otherwise it refreshes every 750 ms or whenever the artillery extension
    // changes. This bounds ProcessEvent traffic while retaining live telemetry.
    if (dual_mode)
    {
        const std::uintptr_t extension = find_artillery_extension(weapon);
        const ULONGLONG now = frame_ticks;
        static bool previous_f8 = false;
        const bool f8_down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        const bool force_capture = f8_down && !previous_f8;
        previous_f8 = f8_down;
        const bool timed_capture = now - g_native_last_capture_ms >= 750;
        if (force_capture || extension != g_native_last_extension || timed_capture)
        {
            capture_native_ballistics(extension, pitch_deg, sight_units, g_native_diag);
            g_native_last_capture_ms = now;
            g_native_last_extension = extension;
        }
        output.native = g_native_diag;
    }

    // For SPH-2, the reflected source table is only the HUD calibration curve.
    // The native simulation uses the live projectile/weapon stats and is the
    // authoritative pitch-to-impact-distance mapping used by the solver.
    const bool native_table_matches =
        !g_native_table.rows.empty() &&
        g_native_table.data_asset == g_cached_table.data_asset &&
        g_native_table.rows.size() == g_cached_table.rows.size();
    if (dual_mode && native_table_matches)
    {
        const float native_range_m = interpolate_range(g_native_table.rows, sight_units);
        if (std::isfinite(native_range_m) && native_range_m >= 0.f)
            output.range_m = native_range_m;
        // Prefer the live black-box result for the current sight angle over
        // an interpolated row.  This is the exact SimulateImpactDistance
        // value the debug arc is meant to visualize.
        const float exact_range_m = output.native.probe_count > 0
                                        ? output.native.probes[0].impact_distance
                                        : 0.f;
        if (std::isfinite(exact_range_m) && exact_range_m > 0.f)
            output.range_m = exact_range_m;
    }

    // --- diagnostics ---
    // TargetRotation is the *requested* angle. LiveWorldRotation reads back as
    // zero on this weapon, so take the barrel's true orientation from the weapon
    // actor's own scene-component transform instead. If it can't reach what we
    // ask for, these diverge and the shell leaves at a different angle than
    // everything we compute from TargetRotation.
    {
        VehicleAttitude barrel = read_vehicle_attitude(weapon);
        if (barrel.valid)
        {
            output.dbg_live_pitch = barrel.pitch;
            output.dbg_live_yaw = barrel.yaw;
            float live_su = static_cast<float>((static_cast<double>(barrel.pitch) - kBarrelOffset) * 1000.0 * kPi / 180.0);
            output.dbg_live_su = live_su;
            const auto& live_rows = dual_mode && native_table_matches
                                        ? g_native_table.rows
                                        : g_cached_table.rows;
            output.dbg_live_rng = interpolate_range(live_rows, live_su);
        }

        // The weapon root is the mount and doesn't elevate. Walk two levels of
        // AttachChildren and record each component's pitch to find the barrel.
        std::uintptr_t wroot = read<std::uintptr_t>(weapon + offsets::AActor::RootComponent);
        auto comp_attitude = [](std::uintptr_t comp, float& p, float& y) -> bool
        {
            struct
            {
                double X, Y, Z, W;
            } q = read<decltype(q)>(comp + offsets::USceneComponent::ComponentToWorld);
            double n = q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W;
            if (!std::isfinite(n) || n < 0.5 || n > 1.5)
                return false;
            double sinp = std::clamp(2.0 * (q.W * q.Y - q.Z * q.X), -1.0, 1.0);
            p = static_cast<float>(std::asin(sinp) * 180.0 / kPi);
            double siny = 2.0 * (q.W * q.Z + q.X * q.Y);
            double cosy = 1.0 - 2.0 * (q.Y * q.Y + q.Z * q.Z);
            y = static_cast<float>(std::atan2(siny, cosy) * 180.0 / kPi);
            return std::isfinite(p) && std::isfinite(y);
        };

        auto record_component = [&](std::uintptr_t component, int depth, bool socket)
        {
            if (output.dbg_comp_count >= Snapshot::kComps || !is_valid_ptr(reinterpret_cast<void*>(component)))
                return;
            float p = 0.f, y = 0.f;
            (void)comp_attitude(component, p, y);
            struct
            {
                double X, Y, Z;
            } tr = read<decltype(tr)>(component + offsets::USceneComponent::ComponentToWorld + 0x20);
            if (!std::isfinite(tr.X) || !std::isfinite(tr.Y) || !std::isfinite(tr.Z))
                return;
            const int s = output.dbg_comp_count++;
            output.dbg_comp_pitch[s] = p;
            output.dbg_comp_yaw[s] = y;
            output.dbg_comp_depth[s] = depth;
            output.dbg_comp_world[s] = {static_cast<float>(tr.X),
                                        static_cast<float>(tr.Y),
                                        static_cast<float>(tr.Z)};
            output.dbg_comp_socket[s] = socket;
            output.dbg_comp_name[s][0] = '\0';
            const FNameValue comp_name = read<FNameValue>(component + offsets::UObject::NamePrivate);
            if (comp_name.ComparisonIndex != 0)
            {
                FString name = engine_funcs::conv_name_to_string(comp_name);
                if (name.IsValid())
                {
                    const std::wstring wide = name.ToWString(63);
                    const std::size_t n = (std::min)(wide.size(), sizeof(output.dbg_comp_name[s]) - 1);
                    for (std::size_t i = 0; i < n; ++i)
                        output.dbg_comp_name[s][i] = wide[i] < 0x80
                                                         ? static_cast<char>(wide[i])
                                                         : '?';
                    output.dbg_comp_name[s][n] = '\0';
                }
                engine_funcs::release_string(name);
            }
            if (!output.dbg_comp_name[s][0])
                std::snprintf(output.dbg_comp_name[s], sizeof(output.dbg_comp_name[s]), "component_%d", s);
        };

        if (is_valid_ptr(reinterpret_cast<void*>(wroot)))
        {
            // Include the weapon root and every attached component so the
            // overlay exposes the actual barrel hierarchy and names.
            record_component(wroot, 0, false);
            TArray<std::uintptr_t> kids = read<TArray<std::uintptr_t>>(wroot + offsets::USceneComponent::AttachChildren);
            if (kids.IsSane(64))
            {
                for (int i = 0; i < kids.Num() && output.dbg_comp_count < Snapshot::kComps; i++)
                {
                    std::uintptr_t c = kids[i];
                    record_component(c, 1, false);
                    TArray<std::uintptr_t> sub = read<TArray<std::uintptr_t>>(c + offsets::USceneComponent::AttachChildren);
                    if (!sub.IsSane(64))
                        continue;
                    for (int j = 0; j < sub.Num() && output.dbg_comp_count < Snapshot::kComps; j++)
                        record_component(sub[j], 2, false);
                }
            }
        }
    }
    output.dbg_sight_units = sight_units;
    output.dbg_row_count = static_cast<int>(g_cached_table.rows.size());
    output.dbg_peak_index = static_cast<int>(g_cached_table.peak_index);
    output.dbg_is_dual = g_cached_table.peak_index > 0 &&
                         g_cached_table.peak_index < g_cached_table.rows.size() - 1;
    if (!g_cached_table.rows.empty())
    {
        output.dbg_first_unit = g_cached_table.rows.front().unit_raw;
        output.dbg_first_dist = g_cached_table.rows.front().distance_m;
        output.dbg_last_unit = g_cached_table.rows.back().unit_raw;
        output.dbg_last_dist = g_cached_table.rows.back().distance_m;
        const auto& pk = g_cached_table.rows[g_cached_table.peak_index];
        output.dbg_peak_unit = pk.unit_raw;
        output.dbg_peak_dist = pk.distance_m;
    }
    {
        TArray<std::uintptr_t> exts = read<TArray<std::uintptr_t>>(weapon + offsets::AWDVehicleWeapon::WeaponExtensions);
        if (exts.IsSane(16))
        {
            for (int e = 0; e < exts.Num(); e++)
            {
                std::uintptr_t ex = exts[e];
                if (!is_valid_ptr(reinterpret_cast<void*>(ex)))
                    continue;
                std::uintptr_t da = read<std::uintptr_t>(ex + offsets::UWDVehicleWeaponExtension::DataAsset);
                if (is_artillery_data(da))
                    output.dbg_artillery_ext++;
            }
        }
    }
    // Sample the table evenly so the raw curve is visible in the overlay.
    {
        const int total = static_cast<int>(g_cached_table.rows.size());
        const int want = Snapshot::kSamples;
        for (int s = 0; s < want && total > 0; s++)
        {
            int idx = (total == 1) ? 0 : (s * (total - 1)) / (want - 1);
            const auto& r = g_cached_table.rows[idx];
            output.dbg_sample_i[s] = idx;
            output.dbg_sample_u[s] = r.unit_raw;
            output.dbg_sample_d[s] = r.distance_m;
            output.dbg_sample_x[s] = r.flight_time_s;
            output.dbg_sample_count = s + 1;
        }
    }

    const auto& bounds_rows = dual_mode && native_table_matches
                                  ? g_native_table.rows
                                  : g_cached_table.rows;
    float mn = (std::numeric_limits<float>::max)(), mx = 0.f;
    for (const auto& r : bounds_rows)
    {
        if (r.distance_m < mn)
            mn = r.distance_m;
        if (r.distance_m > mx)
            mx = r.distance_m;
    }
    output.min_range_m = mn;
    output.max_range_m = mx;

    std::uintptr_t wpn_root = read<std::uintptr_t>(weapon + offsets::AActor::RootComponent);
    VehicleAttitude muzzle_attitude{};
    bool muzzle_attitude_from_barrel = false;
    if (is_valid_ptr(reinterpret_cast<void*>(wpn_root)))
    {
        struct
        {
            double X, Y, Z;
        } tr = read<decltype(tr)>(wpn_root + offsets::USceneComponent::ComponentToWorld + 0x20);
        output.weapon_world_pos = {static_cast<float>(tr.X),
                                   static_cast<float>(tr.Y),
                                   static_cast<float>(tr.Z)};
        const FVector weapon_root_world_pos = output.weapon_world_pos;

        // Enumerate every socket on the weapon mesh.  The previous hard-coded
        // S_Muzzle probe resolved to the turret body on this asset; the live
        // socket census makes the actual barrel endpoint visible and selects
        // only a named muzzle/barrel socket that is displaced from the mount.
        const std::uintptr_t weapon_mesh = read<std::uintptr_t>(weapon + offsets::AWDVehicleWeapon::SkeletalMesh);
        double best_named_offset = 0.0;
        bool exact_muzzle_socket_found = false;
        bool exact_barrel_attach_found = false;
        FVector best_named_socket = output.weapon_world_pos;
        FVector barrel_attach_socket{};
        if (is_valid_ptr(reinterpret_cast<void*>(weapon_mesh)))
        {
            const TArray<FNameValue> sockets = engine_funcs::get_all_socket_names(reinterpret_cast<void*>(weapon_mesh));
            if (sockets.IsSane(Snapshot::kSockets))
            {
                for (int i = 0; i < sockets.Num() && output.dbg_socket_count < Snapshot::kSockets; ++i)
                {
                    const FNameValue socket_name = sockets[i];
                    if (socket_name.ComparisonIndex == 0)
                        continue;
                    const FVector socket_pos = engine_funcs::get_socket_location(reinterpret_cast<void*>(weapon_mesh), socket_name);
                    if (!std::isfinite(socket_pos.X) || !std::isfinite(socket_pos.Y) || !std::isfinite(socket_pos.Z))
                        continue;
                    const int s = output.dbg_socket_count++;
                    output.dbg_socket_world[s] = socket_pos;
                    output.dbg_socket_name[s][0] = '\0';
                    FString socket_string = engine_funcs::conv_name_to_string(socket_name);
                    if (socket_string.IsValid())
                    {
                        const std::wstring wide = socket_string.ToWString(63);
                        const std::size_t n = (std::min)(wide.size(), sizeof(output.dbg_socket_name[s]) - 1);
                        for (std::size_t k = 0; k < n; ++k)
                            output.dbg_socket_name[s][k] = wide[k] < 0x80
                                                               ? static_cast<char>(wide[k])
                                                               : '?';
                        output.dbg_socket_name[s][n] = '\0';
                    }
                    engine_funcs::release_string(socket_string);
                    char lower[64]{};
                    for (std::size_t k = 0; k < sizeof(lower) - 1 && output.dbg_socket_name[s][k]; ++k)
                        lower[k] = static_cast<char>(std::tolower(static_cast<unsigned char>(output.dbg_socket_name[s][k])));
                    const bool exact_muzzle_socket =
                        std::strcmp(lower, "muzzleflash_01_socket") == 0;
                    const bool exact_barrel_attach_socket =
                        std::strcmp(lower, "turretbarrel_01_attach") == 0;
                    const bool named = std::strstr(lower, "muzzle") ||
                                       std::strstr(lower, "barrel") || std::strstr(lower, "projectile") ||
                                       std::strstr(lower, "exit");
                    const double dx = socket_pos.X - output.weapon_world_pos.X;
                    const double dy = socket_pos.Y - output.weapon_world_pos.Y;
                    const double dz = socket_pos.Z - output.weapon_world_pos.Z;
                    const double offset = std::sqrt(dx * dx + dy * dy + dz * dz);
                    // This asset exposes both muzzleFlash_01_Socket and
                    // turretBarrel_01_Attach.  The former is the projectile
                    // spawn socket; prefer it explicitly instead of choosing
                    // whichever named socket happens to be farthest from the
                    // turret mount.
                    if (exact_muzzle_socket && std::isfinite(offset) && offset >= 1.0)
                    {
                        best_named_offset = offset;
                        best_named_socket = socket_pos;
                        exact_muzzle_socket_found = true;
                    }
                    else if (!exact_muzzle_socket_found && named && std::isfinite(offset) && offset > best_named_offset)
                    {
                        best_named_offset = offset;
                        best_named_socket = socket_pos;
                    }
                    if (exact_barrel_attach_socket)
                    {
                        barrel_attach_socket = socket_pos;
                        exact_barrel_attach_found = true;
                    }
                }
            }
        }
        if (dual_mode && best_named_offset >= 25.0)
        {
            output.weapon_world_pos = best_named_socket;
            output.sph_origin_from_socket = true;
        }

        // Socket rotations on this skeletal mesh use an authoring axis that is
        // unrelated to the projectile direction (GetSocketRotation reports
        // roughly 0 degrees while the tube is near 80 degrees).  Derive the
        // firing axis geometrically from the known barrel-base socket to the
        // exact muzzle socket instead.
        if (exact_muzzle_socket_found && exact_barrel_attach_found)
        {
            const double ax = best_named_socket.X - barrel_attach_socket.X;
            const double ay = best_named_socket.Y - barrel_attach_socket.Y;
            const double az = best_named_socket.Z - barrel_attach_socket.Z;
            const double horizontal = std::sqrt(ax * ax + ay * ay);
            const double length = std::sqrt(horizontal * horizontal + az * az);
            if (std::isfinite(length) && length >= 25.0 && horizontal > 0.01)
            {
                muzzle_attitude.pitch = static_cast<float>(std::atan2(az, horizontal) * 180.0 / kPi);
                muzzle_attitude.yaw = static_cast<float>(std::atan2(ay, ax) * 180.0 / kPi);
                muzzle_attitude.valid = std::isfinite(muzzle_attitude.pitch) &&
                                        std::isfinite(muzzle_attitude.yaw);
            }
        }

        // Fallback for builds/assets without a muzzle socket: use the furthest
        // sane barrel child, but expose the source in the debug panel.
        FVector barrel_pos = output.weapon_world_pos;
        double barrel_offset_cm = 0.0;
        auto consider_barrel = [&](std::uintptr_t component)
        {
            if (!is_valid_ptr(reinterpret_cast<void*>(component)))
                return;
            struct
            {
                double X, Y, Z;
            } p = read<decltype(p)>(component + offsets::USceneComponent::ComponentToWorld + 0x20);
            if (!std::isfinite(p.X) || !std::isfinite(p.Y) || !std::isfinite(p.Z))
                return;
            const double dx = p.X - output.weapon_world_pos.X;
            const double dy = p.Y - output.weapon_world_pos.Y;
            const double dz = p.Z - output.weapon_world_pos.Z;
            const double offset = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (offset > barrel_offset_cm && offset < 5000.0)
            {
                barrel_offset_cm = offset;
                barrel_pos = {static_cast<float>(p.X), static_cast<float>(p.Y),
                              static_cast<float>(p.Z)};
                muzzle_attitude = read_component_attitude(component);
                muzzle_attitude_from_barrel = barrel_offset_cm >= 10.0;
            }
        };
        TArray<std::uintptr_t> kids = read<TArray<std::uintptr_t>>(wpn_root + offsets::USceneComponent::AttachChildren);
        if (kids.IsSane(64))
        {
            for (int i = 0; i < kids.Num(); ++i)
            {
                consider_barrel(kids[i]);
                if (!is_valid_ptr(reinterpret_cast<void*>(kids[i])))
                    continue;
                TArray<std::uintptr_t> sub = read<TArray<std::uintptr_t>>(kids[i] + offsets::USceneComponent::AttachChildren);
                if (!sub.IsSane(64))
                    continue;
                for (int j = 0; j < sub.Num(); ++j)
                    consider_barrel(sub[j]);
            }
        }

        // The child-component fallback above can overwrite the attitude we
        // derived from the two authoritative sockets.  Restore the geometric
        // barrel axis after that scan so the exact muzzle path remains the
        // source for SPH-2 as well.
        if (exact_muzzle_socket_found && exact_barrel_attach_found)
        {
            const double ax = best_named_socket.X - barrel_attach_socket.X;
            const double ay = best_named_socket.Y - barrel_attach_socket.Y;
            const double az = best_named_socket.Z - barrel_attach_socket.Z;
            const double horizontal = std::sqrt(ax * ax + ay * ay);
            const double length = std::sqrt(horizontal * horizontal + az * az);
            if (std::isfinite(length) && length >= 25.0 && horizontal > 0.01)
            {
                muzzle_attitude.pitch = static_cast<float>(std::atan2(az, horizontal) * 180.0 / kPi);
                muzzle_attitude.yaw = static_cast<float>(std::atan2(ay, ax) * 180.0 / kPi);
                muzzle_attitude.valid = std::isfinite(muzzle_attitude.pitch) &&
                                        std::isfinite(muzzle_attitude.yaw);
            }
        }
        if (dual_mode && !output.sph_origin_from_socket && barrel_offset_cm >= 10.0)
            output.weapon_world_pos = barrel_pos;

        if (!dual_mode || (!output.sph_origin_from_socket && !muzzle_attitude_from_barrel))
            muzzle_attitude.valid = false;

        if (dual_mode && !output.sph_origin_from_socket && muzzle_attitude_from_barrel)
        {
            // Mortar meshes often expose no named muzzle socket.  Their
            // furthest barrel child is still a reliable geometric endpoint;
            // derive the firing axis from the mount position to that endpoint
            // instead of trusting the child's authored quaternion axis.
            const double ax = barrel_pos.X - weapon_root_world_pos.X;
            const double ay = barrel_pos.Y - weapon_root_world_pos.Y;
            const double az = barrel_pos.Z - weapon_root_world_pos.Z;
            const double horizontal = std::sqrt(ax * ax + ay * ay);
            const double length = std::sqrt(horizontal * horizontal + az * az);
            if (std::isfinite(length) && length >= 10.0 && horizontal > 0.01)
            {
                muzzle_attitude.pitch = static_cast<float>(std::atan2(az, horizontal) * 180.0 / kPi);
                muzzle_attitude.yaw = static_cast<float>(std::atan2(ay, ax) * 180.0 / kPi);
                muzzle_attitude.valid = std::isfinite(muzzle_attitude.pitch) &&
                                        std::isfinite(muzzle_attitude.yaw);
            }
        }

        // The muzzleFlash_01_Socket is the projectile origin, but the authored
        // barrel-to-muzzle socket offset is not an elevation reference on
        // this mesh.  Keep TargetRotation + vehicle attitude as the launch
        // angle; using that offset here makes the shell leave above the arc.
    }

    // Keep a target-independent SPH-2 arc alive whenever
    // SimulateImpactDistance produced a usable range.  auto_target replaces
    // it with the selected solution when a target is available.
    const bool native_probe_ready = output.native.probe_count > 0 &&
                                    output.native.probes[0].ok &&
                                    std::isfinite(output.native.probes[0].impact_distance) &&
                                    output.native.probes[0].impact_distance > 0.f;
    if (dual_mode && (native_table_matches || native_probe_ready) && output.weapon_world_pos.Length() > 0.0)
    {
        const auto& arc_rows = native_table_matches ? g_native_table.rows
                                                    : g_cached_table.rows;
        // The native probe is sampled from the exact sight units that the
        // artillery component reports.  Re-running the simulation with a
        // quaternion pitch from an attached child produced the observed
        // 3-m/0-m readings even while the probe itself reported ~974 m; that
        // child quaternion is not in the SPH sight-unit convention.  Keep the
        // barrel child only as the launch origin and use the probed angle and
        // distance as one consistent pair for the marker/arc.
        float arc_sight_units = sight_units;
        float arc_world_yaw = static_cast<float>(target_rot.Yaw) + veh_att.yaw;
        // The named muzzle socket supplies the actual world launch axis for
        // the trajectory preview.  Keep the native solve on TargetRotation's
        // sight units, but draw/measure the arc from the same axis the shell
        // leaves from so a mesh mount offset cannot hide a pitch error.
        if (muzzle_attitude.valid && output.sph_origin_from_socket)
        {
            arc_sight_units = static_cast<float>((static_cast<double>(muzzle_attitude.pitch) - kBarrelOffset) * 1000.0 * kPi / 180.0);
            arc_world_yaw = muzzle_attitude.yaw;
        }
        float exact = (output.native.probe_count > 0)
                          ? output.native.probes[0].impact_distance
                          : 0.f;
        const float nominal = (std::isfinite(exact) && exact > 0.f)
                                  ? exact
                                  : interpolate_range(arc_rows, sight_units);
        build_sph_debug_arc(output, arc_sight_units, arc_world_yaw, 0.f, nominal, automatic_sph2_range_scale(dual_mode));
    }
    else if (!dual_mode && output.range_m > 1.f && output.weapon_world_pos.Length() > 0.0)
    {
        // The L81/hand mortar has a single monotonic calibration curve.  Draw
        // that reflected curve directly; do not substitute the SPH-2 native
        // probe or socket geometry.  This keeps the overlay at the same 110 m
        // class of value shown by the mortar's own optic ruler.
        build_sph_debug_arc(output, sight_units, static_cast<float>(target_rot.Yaw) + veh_att.yaw, 0.f, output.range_m, 1.f);
    }

    return true;
}

void wdgs::mortar_aim::auto_target(Snapshot& snap, const std::vector<TargetPlayer>& targets, bool key_down, bool team_check, int smooth, float fov_deg, float range_scale, int arc_mode, bool menu_visible)
{
    // Artillery must command the exact table solution. Interpolating the
    // requested rotation with the infantry-aim smoothing leaves tens of mils
    // of range error while the UI already reports a valid solution.
    static_cast<void>(smooth);
    // The SPH-2 calibration is automatic.  Keep the argument for ABI/source
    // compatibility with existing callers, but never let a menu value alter
    // the field model.
    static_cast<void>(range_scale);
    // A fixed 0.89-style multiplier was only valid for one elevation.  Keep
    // the native angle-dependent simulation in metres and solve at unity.
    range_scale = 1.0f;

    snap.has_target = false;
    snap.candidates.clear();

    // A locked NUM9 marker is also a valid fire-control target.  It may be
    // deliberately outside the camera FOV after selecting a different
    // indirect angle, so it must not depend on a projected actor centre.
    std::vector<TargetPlayer> marker_targets;
    const bool marker_only = targets.empty() && snap.sph_marker_locked &&
                             std::isfinite(snap.sph_marker_world.X) &&
                             std::isfinite(snap.sph_marker_world.Y) &&
                             std::isfinite(snap.sph_marker_world.Z);
    if (marker_only)
    {
        TargetPlayer marker{};
        marker.position = snap.sph_marker_world;
        marker.actor_addr = 0;
        marker.health = 1.f;
        wcsncpy_s(marker.name, L"SPH2 MARKER", _TRUNCATE);
        marker_targets.push_back(marker);
    }
    const auto& aim_targets = marker_only ? marker_targets : targets;
    if (!snap.valid || aim_targets.empty())
        return;

    const FVector& mortar = snap.weapon_world_pos;
    if (mortar.X == 0.0 && mortar.Y == 0.0 && mortar.Z == 0.0)
        return;
    // Read current turret rotation
    std::uintptr_t rot_comp = read<std::uintptr_t>(snap.weapon_ptr + offsets::AWDVehicleWeapon::RotationComponent);
    if (!is_valid_ptr(reinterpret_cast<void*>(rot_comp)))
        return;

    FRotator current = read<FRotator>(rot_comp + offsets::UWDWeaponRotationComponent::TargetRotation);
    float current_world_yaw = static_cast<float>(current.Yaw) + snap.vehicle_yaw;
    // Build candidate list
    const float field_range_scale = snap.dbg_is_dual ? range_scale : 1.f;
    const float range_min_cm = snap.min_range_m * field_range_scale * 100.f;
    if (snap.dbg_is_dual)
        snap.range_m *= field_range_scale;
    const float half_fov = fov_deg * 0.5f;

    struct Candidate
    {
        const TargetPlayer* tp;
        float dist_m;
        float angle;
    };
    std::vector<Candidate> valid;

    for (const auto& t : aim_targets)
    {
        if (team_check && t.is_team)
            continue;
        if (t.health <= 0.f)
            continue;

        float ddx = t.position.X - mortar.X;
        float ddy = t.position.Y - mortar.Y;
        float d = std::sqrtf(ddx * ddx + ddy * ddy);
        if (d < range_min_cm)
            continue;

        float yaw_to = static_cast<float>(std::atan2(ddy, ddx) * 180.0 / kPi);
        float diff = yaw_to - current_world_yaw;
        while (diff > 180.f)
            diff -= 360.f;
        while (diff < -180.f)
            diff += 360.f;
        if (!marker_only && std::fabs(diff) > half_fov)
            continue;

        valid.push_back({&t, d / 100.f, std::fabs(diff)});
    }

    std::sort(valid.begin(), valid.end(), [](const Candidate& a, const Candidate& b)
              { return a.angle < b.angle; });

    for (const auto& c : valid)
    {
        CandidateInfo ci{};
        wcsncpy_s(ci.name, c.tp->name, _TRUNCATE);
        ci.distance_m = c.dist_m;
        ci.actor_addr = c.tp->actor_addr;
        ci.in_range = c.dist_m <= snap.max_range_m * field_range_scale;
        snap.candidates.push_back(ci);
    }

    if (snap.candidates.empty())
        return;
    // Selection cycling (PgUp / PgDn)
    int n = static_cast<int>(snap.candidates.size());

    if (s_selected_actor != 0)
    {
        bool found_sel = false;
        for (int i = 0; i < n; i++)
        {
            if (snap.candidates[i].actor_addr == s_selected_actor)
            {
                s_selected_index = i;
                found_sel = true;
                break;
            }
        }
        if (!found_sel)
        {
            s_selected_index = 0;
            s_selected_actor = snap.candidates[0].actor_addr;
        }
    }
    else
    {
        s_selected_index = 0;
        s_selected_actor = snap.candidates[0].actor_addr;
    }

    bool pgup_now = !menu_visible && (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
    bool pgdn_now = !menu_visible && (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
    if (pgup_now && !prev_pgup)
        s_selected_index = (s_selected_index - 1 + n) % n;
    if (pgdn_now && !prev_pgdn)
        s_selected_index = (s_selected_index + 1) % n;
    prev_pgup = pgup_now;
    prev_pgdn = pgdn_now;

    if (s_selected_index < 0 || s_selected_index >= n)
        s_selected_index = 0;
    s_selected_actor = snap.candidates[s_selected_index].actor_addr;

    snap.selected_index = s_selected_index;
    snap.selected_actor = s_selected_actor;
    // Solve the selected target.  The solution is also used by the SPH debug
    // arc while the aim key is up; only the actual rotation write is gated by
    // key_down.
    const TargetPlayer* best = nullptr;
    for (const auto& t : aim_targets)
    {
        if (t.actor_addr == snap.selected_actor)
        {
            best = &t;
            break;
        }
    }
    if (!best)
        return;

    if (g_cached_table.rows.empty())
        return;

    // A range table whose peak sits in the MIDDLE has two branches (direct +
    // indirect) — that's the SPH-2 dual-mode cannon. A plain mortar's table is
    // monotonic (peak at an end). Only the SPH-2 gets the special handling
    // (branch split, native ballistic table, time-of-flight telemetry); the mortar
    // keeps its original, known-good behaviour untouched.
    const bool is_dual_mode =
        g_cached_table.peak_index > 0 &&
        g_cached_table.peak_index < g_cached_table.rows.size() - 1;

    const bool native_table_matches =
        is_dual_mode && !g_native_table.rows.empty() &&
        g_native_table.data_asset == g_cached_table.data_asset &&
        g_native_table.rows.size() == g_cached_table.rows.size();
    const CachedTable& solver_table = native_table_matches
                                          ? g_native_table
                                          : g_cached_table;

    // When the SPH-2 marker is locked, it is the fire-control impact point.
    // Do not rotate the turret toward the actor's screen/mesh centre: at the
    // steep indirect elevation the muzzle axis is laterally displaced from
    // that centre and the round would miss the marked point.  The marker is
    // stored in world space, so using it here keeps yaw, range, pitch and the
    // rendered arc on one exact point.
    const bool marker_aim = snap.sph_marker_locked &&
                            std::isfinite(snap.sph_marker_world.X) &&
                            std::isfinite(snap.sph_marker_world.Y) &&
                            std::isfinite(snap.sph_marker_world.Z) &&
                            (snap.sph_marker_world.X != 0.0 || snap.sph_marker_world.Y != 0.0);
    const float aim_x = marker_aim ? snap.sph_marker_world.X : best->position.X;
    const float aim_y = marker_aim ? snap.sph_marker_world.Y : best->position.Y;
    const float aim_z = marker_aim ? snap.sph_marker_world.Z : best->position.Z;
    float dx = aim_x - mortar.X;
    float dy = aim_y - mortar.Y;
    float horiz_dist_m = std::sqrtf(dx * dx + dy * dy) / 100.f;
    if (horiz_dist_m < 1.f)
        return;

    float target_su = 0.f;
    float flight_time = 0.f;
    const float delta_z_m = static_cast<float>(aim_z - mortar.Z) / 100.f;

    if (!is_dual_mode)
    {
        target_su = inverse_interpolate(g_cached_table.rows, horiz_dist_m);
    }
    else
    {
        // Use the branch explicitly selected in the menu. TargetRotation is a
        // requested angle and may already contain the previous automatic command;
        // deriving the mode from it made an indirect setup fall back to direct.
        bool indirect_arc = arc_mode == 1;
        // If the operator changes elevation manually, the live barrel branch
        // is authoritative.  Otherwise a menu value left on direct-fire
        // keeps solving the low branch while the barrel is already on the
        // high branch, which sends the shot away from the marker.
        if (!indirect_arc && snap.sight_mils > snap.dbg_peak_unit && std::isfinite(snap.sight_mils) && std::isfinite(snap.dbg_peak_unit))
            indirect_arc = true;
        // SimulateImpactDistance supplies the field curve used by both the HUD
        // and the solver. The inverse therefore uses the same automatic scale
        // as the live probe instead of a user-configured compensation.
        const float applied_range_scale = range_scale;
        snap.dbg_applied_range_scale = applied_range_scale;
        std::size_t search_start, search_end;
        if (!indirect_arc)
        {
            search_start = 0;
            search_end = solver_table.peak_index;
        }
        else
        {
            search_start = solver_table.peak_index;
            search_end = solver_table.rows.size() - 1;
        }

        // SPH-2 fire control uses the selected player's current position only.
        // Target velocity is intentionally ignored: artillery flight time is
        // retained solely as telemetry and never changes range or azimuth.
        target_su = solve_height_corrected_units(solver_table.rows, horiz_dist_m, delta_z_m, applied_range_scale, search_start, search_end);
        // The analytic height correction above is only a fallback. When the
        // live projectile/weapon buffers are available, solve the same target
        // distance through SimulateImpactDistance with InitialHeightMeters =
        // -target_delta_z. That native call includes the game's gravity,
        // drag/air-density, mass, diameter, thrust and muzzle-speed fields.
        if (native_table_matches)
        {
            target_su = solve_native_height_units(horiz_dist_m, delta_z_m, search_start, search_end, target_su);
        }
        flight_time = interpolate_flight_time(g_cached_table.rows, target_su);
        if (!std::isfinite(flight_time) || flight_time < 0.f)
            flight_time = 0.f;
    }

    const float desired_world_pitch = static_cast<float>(target_su * 180.0 / (1000.0 * kPi) + kBarrelOffset);
    const float desired_world_yaw = static_cast<float>(std::atan2(dy, dx) * 180.0 / kPi);

    // TargetRotation is expressed in the weapon mount's local frame.  The
    // mount has a yaw/pitch offset from the vehicle frame, and that offset
    // changes when the barrel is elevated.  Subtracting only vehicle_yaw /
    // vehicle_pitch therefore makes the shell leave along a parallel line
    // (the marker can be hundreds of metres to the side).  Recover the live
    // muzzle axis from the rendered world-space arc and command a delta from
    // that axis, preserving the mount offset for every selected elevation.
    float live_axis_yaw = current_world_yaw;
    float live_axis_pitch = current.Pitch + snap.vehicle_pitch;
    if (snap.sph_arc_world_count >= 2)
    {
        const FVector axis = snap.sph_arc_world[1] - snap.sph_arc_world[0];
        const float horizontal = std::sqrtf(axis.X * axis.X + axis.Y * axis.Y);
        if (std::isfinite(horizontal) && horizontal > 0.01f && std::isfinite(axis.Z))
        {
            live_axis_yaw = static_cast<float>(std::atan2(axis.Y, axis.X) * 180.0 / kPi);
            live_axis_pitch = static_cast<float>(std::atan2(axis.Z, horizontal) * 180.0 / kPi);
        }
    }
    float yaw_delta = desired_world_yaw - live_axis_yaw;
    while (yaw_delta > 180.f)
        yaw_delta -= 360.f;
    while (yaw_delta < -180.f)
        yaw_delta += 360.f;
    float pitch_delta = desired_world_pitch - live_axis_pitch;
    float target_pitch = static_cast<float>(current.Pitch) + pitch_delta;
    float target_yaw = static_cast<float>(current.Yaw) + yaw_delta;
    while (target_yaw > 180.f)
        target_yaw -= 360.f;
    while (target_yaw < -180.f)
        target_yaw += 360.f;

    // --- diagnostics ---
    {
        float dz = aim_z - mortar.Z;
        snap.dbg_target_dz_m = delta_z_m;
        snap.dbg_slant_dist_m = std::sqrtf(dx * dx + dy * dy + dz * dz) / 100.f;
        snap.dbg_target_units = target_su;
        snap.dbg_target_pitch = target_pitch;
        snap.dbg_pitch_error = target_pitch - static_cast<float>(current.Pitch);
        snap.dbg_cur_yaw = current_world_yaw;
        snap.dbg_target_yaw = target_yaw;
        snap.dbg_target_yaw_w = static_cast<float>(std::atan2(dy, dx) * 180.0 / kPi);
        snap.dbg_flight_time = flight_time;
        float lx = aim_x - best->position.X;
        float ly = aim_y - best->position.Y;
        snap.dbg_lead_m = std::sqrtf(lx * lx + ly * ly) / 100.f;
        snap.dbg_veh_pitch = snap.vehicle_pitch;
        snap.dbg_veh_yaw = snap.vehicle_yaw;
    }

    snap.target_range_m = horiz_dist_m;
    snap.has_target = true;

    // The selected target's horizontal distance is the endpoint. Using a
    // table range here made the debug arc drift away from the target whenever
    // the height-aware native solve changed the required sight units.
    const float nominal_arc_range = horiz_dist_m;
    build_sph_debug_arc(snap, target_su, static_cast<float>(std::atan2(dy, dx) * 180.0 / kPi), delta_z_m, nominal_arc_range, 1.f);

    if (!key_down)
        return;

    FRotator new_rot;
    new_rot.Pitch = target_pitch;
    new_rot.Yaw = target_yaw;
    new_rot.Roll = current.Roll;

    write<FRotator>(rot_comp + offsets::UWDWeaponRotationComponent::TargetRotation, new_rot);
}
