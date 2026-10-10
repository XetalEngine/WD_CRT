#include "stdafx.h"
#include "game.h"
#include "offsets.h"
#include "classes.h"
#include "player_snapshot.h"
#include "engine_funcs.h"
#include "aimbot.h"
#include "config.h"
#include "actor_registry.h"
#include "world_actor_snapshot.h"
#include "weapon_stats.h"
#include "mortar_aim.h"
#include "magic_bullet.h"
#include "anti_sam.h"
#include "hook_process_event.h"
#include "extras.h"
#include "tracers.h"
#include "game_actions.h"
#include "visual_math.h"

#include <cmath>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace game
{
    namespace
    {

        struct MatchContext
        {
            std::uintptr_t world = 0, level = 0, controller = 0, pawn = 0, camera = 0, player_state = 0;
        } g_match;
        wchar_t g_local_name[65]{};
        ULONGLONG g_next_name_read = 0;

        bool live(std::uintptr_t object)
        {
            return engine::is_live_object(reinterpret_cast<const void*>(object));
        }

        bool current_match(const MatchContext& match)
        {
            return read<std::uintptr_t>(offsets::base + offsets::UWorldPtr) == match.world && live(match.world) && read<std::uintptr_t>(match.world + offsets::World::PersistentLevel) == match.level && live(match.controller) && read<std::uintptr_t>(match.controller + offsets::APlayerController_Extra::ControllerPawn) == match.pawn && live(match.pawn) && read<std::uintptr_t>(match.player_state + offsets::APlayerState::PawnPrivate) == match.pawn;
        }

        bool read_match(MatchContext& match)
        {
            // No native engine calls until a local gameplay pawn and its ownership link exist.
            match.world = read<std::uintptr_t>(offsets::base + offsets::UWorldPtr);
            if (!live(match.world))
                return false;
            match.level = read<std::uintptr_t>(match.world + offsets::World::PersistentLevel);
            const auto instance = read<std::uintptr_t>(match.world + offsets::World::OwningGameInstance);
            const auto game_state = read<std::uintptr_t>(match.world + offsets::World::GameState);
            if (!live(match.level) || !live(instance) || !live(game_state))
                return false;
            const auto players = read<TArray<std::uintptr_t>>(instance + offsets::UGameInstance::LocalPlayers);
            std::uintptr_t local_player = 0;
            if (!players.TryGet(0, local_player, 8) || !live(local_player))
                return false;
            match.controller = read<std::uintptr_t>(local_player + offsets::UPlayer::PlayerController);
            if (!live(match.controller))
                return false;
            match.pawn = read<std::uintptr_t>(match.controller + offsets::APlayerController_Extra::ControllerPawn);
            match.camera = read<std::uintptr_t>(match.controller + offsets::APlayerController::PlayerCameraManager);
            if (!live(match.pawn) || !live(match.camera))
                return false;
            match.player_state = read<std::uintptr_t>(match.pawn + offsets::APawn::PlayerState);
            if (!live(match.player_state) || !live(read<std::uintptr_t>(match.pawn + offsets::ACharacter::Mesh)))
                return false;
            return current_match(match);
        }

        std::uintptr_t g_countermeasure_vehicle = 0;
        bool g_countermeasure_vehicle_is_rotary = false;
        std::uint64_t g_countermeasure_vehicle_last_seen_ms = 0;
        constexpr std::uint64_t kCountermeasureVehicleGraceMs = 5000;

        bool finite(const FVector& value)
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }

        bool finite(const FRotator& value)
        {
            return std::isfinite(value.Pitch) && std::isfinite(value.Yaw) && std::isfinite(value.Roll);
        }

        bool read_camera_cache(std::uintptr_t manager, CameraIPC& camera)
        {
            // Same contiguous POV used by spot's GetCameraPOV; no ProcessEvent.
            constexpr auto size = offsetof(CameraIPC, fov) + sizeof(float);
            static_assert(offsetof(CameraIPC, rotation) == 0x18 && offsetof(CameraIPC, fov) == 0x30);
            const auto address = manager + offsets::APlayerCameraManager::CameraCachePrivate + offsets::FCameraCacheEntry::POV;
            CameraIPC first{}, second{};
            if (!read_mem(address, &first, size) || !read_mem(address, &second, size) || memcmp(&first, &second, size) || !finite(second.location) || !finite(second.rotation) || !std::isfinite(second.fov) || second.fov <= 1 || second.fov >= 179)
                return false;
            camera = second;
            return true;
        }

        struct ActorObjectCacheEntry
        {
            FNameValue actor_name{};
            wdgs::actors::Kind kind = wdgs::actors::Kind::none;
            std::string label;
            bool vehicle_probe_done = false;
            bool dynamic_vehicle_label = false;
            std::uint64_t last_vehicle_probe_frame = 0;
            std::uint64_t last_seen_frame = 0;
        };

        constexpr std::uint64_t kActorCachePruneInterval = 300;
        constexpr std::uint64_t kActorCacheRetainFrames = 900;
        std::unordered_map<std::uintptr_t, ActorObjectCacheEntry> g_actor_objects;
        // FName resolution is asynchronous and can briefly return an unclassified
        // actor during map/lobby transitions.  A verified PlayerState -> PawnPrivate
        // ownership link is a stronger player signal; remember its name index so the
        // expensive structural check is only needed the first time that class appears.
        std::unordered_set<std::uint32_t> g_verified_player_names;
        std::uint64_t g_actor_cache_frame = 1;
        void reset_actor_object_cache()
        {
            g_actor_objects.clear();
            g_actor_objects.reserve(256);
            g_verified_player_names.clear();
            g_verified_player_names.reserve(16);
            g_actor_cache_frame = 1;
        }

        bool has_verified_player_state_link(std::uintptr_t actor)
        {
            const auto player_state = read<std::uintptr_t>(actor + offsets::APawn::PlayerState);
            if (!is_valid_ptr(reinterpret_cast<void*>(player_state)))
                return false;

            const auto owned_pawn = read<std::uintptr_t>(player_state + offsets::APlayerState::PawnPrivate);
            if (owned_pawn != actor)
                return false;

            const auto mesh = read<std::uintptr_t>(actor + offsets::ACharacter::Mesh);
            const auto root = read<std::uintptr_t>(actor + offsets::AActor::RootComponent);
            return is_valid_ptr(reinterpret_cast<void*>(mesh)) &&
                   is_valid_ptr(reinterpret_cast<void*>(root));
        }

        void begin_actor_object_frame()
        {
            ++g_actor_cache_frame;
            if (g_actor_cache_frame % kActorCachePruneInterval != 0)
                return;

            for (auto it = g_actor_objects.begin(); it != g_actor_objects.end();)
            {
                if (g_actor_cache_frame - it->second.last_seen_frame > kActorCacheRetainFrames)
                    it = g_actor_objects.erase(it);
                else
                    ++it;
            }
        }

        wdgs::actors::Match resolve_actor_match(std::uintptr_t actor, FNameValue actor_name, bool vehicles_enabled)
        {
            auto cached = g_actor_objects.find(actor);
            const bool same_object = cached != g_actor_objects.end() &&
                                     cached->second.actor_name.ComparisonIndex == actor_name.ComparisonIndex &&
                                     cached->second.actor_name.Number == actor_name.Number;
            const bool refresh_dynamic_label = same_object &&
                                               cached->second.dynamic_vehicle_label &&
                                               g_actor_cache_frame - cached->second.last_seen_frame >= 30;
            const bool retry_failed_vehicle_probe = same_object && vehicles_enabled &&
                                                    cached->second.kind == wdgs::actors::Kind::none &&
                                                    cached->second.vehicle_probe_done &&
                                                    g_actor_cache_frame - cached->second.last_vehicle_probe_frame >= 15;
            if (same_object && !refresh_dynamic_label && !retry_failed_vehicle_probe && (cached->second.kind != wdgs::actors::Kind::none || !vehicles_enabled || cached->second.vehicle_probe_done))
            {
                cached->second.last_seen_frame = g_actor_cache_frame;
                return {cached->second.kind,
                        cached->second.label.empty() ? nullptr : cached->second.label.c_str()};
            }

            wdgs::actors::Match match = wdgs::actors::classify(actor_name);
            const bool was_unclassified = match.kind == wdgs::actors::Kind::none;
            bool vehicle_probe_done = false;
            if (match.kind == wdgs::actors::Kind::none && vehicles_enabled)
            {
                vehicle_probe_done = true;
                if (const char* vehicle_label = wdgs::actors::try_get_vehicle_label(actor))
                    match = {wdgs::actors::Kind::heli, vehicle_label};
                else if (const char* stationary_label = wdgs::actors::try_get_stationary_vehicle_label(actor))
                    match = {wdgs::actors::Kind::stationary, stationary_label};
            }

            ActorObjectCacheEntry entry{};
            entry.actor_name = actor_name;
            entry.kind = match.kind;
            entry.label = match.label ? match.label : xor_text("");
            entry.vehicle_probe_done = vehicle_probe_done;
            entry.last_vehicle_probe_frame = vehicle_probe_done
                                                 ? g_actor_cache_frame
                                                 : (same_object ? cached->second.last_vehicle_probe_frame : 0);
            entry.dynamic_vehicle_label = was_unclassified && vehicle_probe_done &&
                                          match.kind != wdgs::actors::Kind::none;
            entry.last_seen_frame = g_actor_cache_frame;
            auto [it, inserted] = g_actor_objects.insert_or_assign(actor, std::move(entry));
            return {it->second.kind,
                    it->second.label.empty() ? nullptr : it->second.label.c_str()};
        }

        bool read_local_yaw(std::uintptr_t pawn, float& yaw_out)
        {
            if (!is_valid_ptr(reinterpret_cast<void*>(pawn)))
                return false;

            void* root = engine_funcs::k2_get_root_component(reinterpret_cast<void*>(pawn));
            if (!root)
                return false;

            FTransform transform{};
            if (!engine_funcs::k2_get_component_to_world(root, transform))
                return false;

            const auto& q = transform.Rotation;
            const double sin_yaw = 2.0 * (q.W * q.Z + q.X * q.Y);
            const double cos_yaw = 1.0 - 2.0 * (q.Y * q.Y + q.Z * q.Z);
            const double yaw = std::atan2(sin_yaw, cos_yaw) * (180.0 / 3.14159265358979323846);
            if (!std::isfinite(yaw))
                return false;

            yaw_out = static_cast<float>(yaw);
            return std::isfinite(yaw_out);
        }

        void collect_weapons_from_actor(std::uintptr_t actor, std::vector<std::uintptr_t>& out)
        {
            constexpr std::uintptr_t kInstComps = 0x290;
            TArray<std::uintptr_t> inst_comps = read<TArray<std::uintptr_t>>(actor + kInstComps);
            if (inst_comps.IsSane(256) && inst_comps.Num() > 0)
            {
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
                        std::uintptr_t weapon = weapons[w];
                        if (!is_valid_ptr(reinterpret_cast<void*>(weapon)))
                            continue;
                        std::uintptr_t rc = read<std::uintptr_t>(weapon + offsets::AWDVehicleWeapon::RotationComponent);
                        if (is_valid_ptr(reinterpret_cast<void*>(rc)))
                            out.push_back(weapon);
                    }
                }
            }
            {
                std::uintptr_t rc = read<std::uintptr_t>(actor + offsets::AWDVehicleWeapon::RotationComponent);
                TArray<std::uintptr_t> ext = read<TArray<std::uintptr_t>>(actor + offsets::AWDVehicleWeapon::WeaponExtensions);
                if (is_valid_ptr(reinterpret_cast<void*>(rc)) && ext.IsSane(16) && ext.Num() > 0)
                    out.push_back(actor);
            }
        }

        void collect_all_vehicle_weapons(std::uintptr_t vehicle, std::vector<std::uintptr_t>& out)
        {
            collect_weapons_from_actor(vehicle, out);

            std::uintptr_t root = read<std::uintptr_t>(vehicle + offsets::AActor::RootComponent);
            if (!is_valid_ptr(reinterpret_cast<void*>(root)))
                return;

            constexpr std::uintptr_t kAttachOffsets[] = {0x108, 0x100, 0x118};
            for (auto off : kAttachOffsets)
            {
                TArray<std::uintptr_t> children = read<TArray<std::uintptr_t>>(root + off);
                if (!children.IsSane(256) || children.Num() <= 0)
                    continue;
                for (int i = 0; i < children.Num(); i++)
                {
                    std::uintptr_t child = children[i];
                    if (!is_valid_ptr(reinterpret_cast<void*>(child)))
                        continue;
                    std::uintptr_t owner = read<std::uintptr_t>(child + offsets::UObject::OuterPrivate);
                    if (!is_valid_ptr(reinterpret_cast<void*>(owner)) || owner == vehicle)
                        continue;
                    collect_weapons_from_actor(owner, out);
                }
            }
        }

        std::uintptr_t pick_weapon_by_camera(const std::vector<std::uintptr_t>& weapons, float cam_yaw, float cam_pitch)
        {
            std::uintptr_t best = 0;
            float best_diff = 9999.f;
            for (auto w : weapons)
            {
                std::uintptr_t rc = read<std::uintptr_t>(w + offsets::AWDVehicleWeapon::RotationComponent);
                if (!is_valid_ptr(reinterpret_cast<void*>(rc)))
                    continue;
                FRotator live = read<FRotator>(rc + offsets::UWDWeaponRotationComponent::LiveWorldRotation);
                float dy = static_cast<float>(live.Yaw) - cam_yaw;
                while (dy > 180.f)
                    dy -= 360.f;
                while (dy < -180.f)
                    dy += 360.f;
                float dp = static_cast<float>(live.Pitch) - cam_pitch;
                float diff = std::fabs(dy) + std::fabs(dp);
                if (diff < best_diff)
                {
                    best_diff = diff;
                    best = w;
                }
            }
            return best;
        }

        void fill_vehicle_quat(std::uintptr_t vehicle, aimbot::VehicleAimContext& ctx)
        {
            std::uintptr_t veh_root = read<std::uintptr_t>(vehicle + offsets::AActor::RootComponent);
            if (!is_valid_ptr(reinterpret_cast<void*>(veh_root)))
                return;
            constexpr double kPi = 3.14159265358979323846;
            struct
            {
                double X, Y, Z, W;
            } quat = read<decltype(quat)>(veh_root + offsets::USceneComponent::ComponentToWorld);
            double sinp = 2.0 * (quat.W * quat.Y - quat.Z * quat.X);
            if (sinp > 1.0)
                sinp = 1.0;
            if (sinp < -1.0)
                sinp = -1.0;
            ctx.vehicle_pitch = static_cast<float>(std::asin(sinp) * 180.0 / kPi);
            double siny = 2.0 * (quat.W * quat.Z + quat.X * quat.Y);
            double cosy = 1.0 - 2.0 * (quat.Y * quat.Y + quat.Z * quat.Z);
            ctx.vehicle_yaw = static_cast<float>(std::atan2(siny, cosy) * 180.0 / kPi);
            double sinr = 2.0 * (quat.W * quat.X + quat.Y * quat.Z);
            double cosr = 1.0 - 2.0 * (quat.X * quat.X + quat.Y * quat.Y);
            ctx.vehicle_roll = static_cast<float>(std::atan2(sinr, cosr) * 180.0 / kPi);
        }

        std::uintptr_t find_occupied_vehicle(std::uintptr_t pawn)
        {
            if (!is_valid_ptr(reinterpret_cast<void*>(pawn)))
                return 0;
            const std::uintptr_t op = read<std::uintptr_t>(pawn + offsets::AWDMoverCharacter::VehicleOperator);
            if (!is_valid_ptr(reinterpret_cast<void*>(op)))
                return 0;
            std::uintptr_t seat = read<std::uintptr_t>(op + offsets::UWDVehicleOperatorComponent::CurrentSeat);
            if (!is_valid_ptr(reinterpret_cast<void*>(seat)))
                seat = read<std::uintptr_t>(op + offsets::UWDVehicleOperatorComponent::ReplicatedSeat);
            if (!is_valid_ptr(reinterpret_cast<void*>(seat)))
                return 0;
            const std::uintptr_t immediate_outer = read<std::uintptr_t>(seat + offsets::UObject::OuterPrivate);
            if (!is_valid_ptr(reinterpret_cast<void*>(immediate_outer)))
                return 0;

            // Seat ownership is not identical across rotary blueprints.  Havoc seats
            // are directly outered to the vehicle, while the Little Bird can insert
            // another seat/component object in the chain.  Returning that wrapper
            // makes K2_GetRootComponent fail and the anti-SAM tracker falls back to the
            // moving camera, which explains the intermittent warning while yawing.
            // Walk the UObject outer chain and select the actual vehicle actor.
            const std::uintptr_t vehicle_base = reinterpret_cast<std::uintptr_t>(engine_funcs::get_vehicle_base_class());
            auto derives_from = [](std::uintptr_t object, std::uintptr_t base_class)
            {
                if (!is_valid_ptr(reinterpret_cast<void*>(object)) || !base_class)
                    return false;
                std::uintptr_t cls = read<std::uintptr_t>(object + offsets::UObject::ClassPrivate);
                for (int depth = 0; depth < 64 && is_valid_ptr(reinterpret_cast<void*>(cls)); ++depth)
                {
                    if (cls == base_class)
                        return true;
                    cls = read<std::uintptr_t>(cls + offsets::UStruct::SuperStruct);
                }
                return false;
            };

            std::uintptr_t candidate = immediate_outer;
            for (int depth = 0; depth < 12 && is_valid_ptr(reinterpret_cast<void*>(candidate)); ++depth)
            {
                if (engine_funcs::is_rotary_vehicle(reinterpret_cast<void*>(candidate)) || derives_from(candidate, vehicle_base))
                {
                    return candidate;
                }
                const std::uintptr_t outer = read<std::uintptr_t>(candidate + offsets::UObject::OuterPrivate);
                if (!is_valid_ptr(reinterpret_cast<void*>(outer)) || outer == candidate)
                    break;
                candidate = outer;
            }

            // Preserve the previous behavior for builds where the vehicle base class
            // has not resolved yet; the next frame retries the complete chain.
            return immediate_outer;
        }

        aimbot::VehicleAimContext find_vehicle_aim_context(std::uintptr_t pawn, std::uintptr_t camera_manager, const FRotator& cam_rotation)
        {
            aimbot::VehicleAimContext ctx{};

            const std::uintptr_t vehicle = find_occupied_vehicle(pawn);
            if (!vehicle)
                return ctx;

            std::uintptr_t vt_target = 0;
            if (is_valid_ptr(reinterpret_cast<void*>(camera_manager)))
            {
                vt_target = read<std::uintptr_t>(camera_manager + offsets::APlayerCameraManager::ViewTarget + offsets::FTViewTarget::Target);
            }

            float cam_yaw = static_cast<float>(cam_rotation.Yaw);
            float cam_pitch = static_cast<float>(cam_rotation.Pitch);

            std::uintptr_t weapon = 0;

            if (is_valid_ptr(reinterpret_cast<void*>(vt_target)) && vt_target != pawn && vt_target != vehicle)
            {
                std::uintptr_t rc = read<std::uintptr_t>(vt_target + offsets::AWDVehicleWeapon::RotationComponent);
                TArray<std::uintptr_t> ext = read<TArray<std::uintptr_t>>(vt_target + offsets::AWDVehicleWeapon::WeaponExtensions);
                if (is_valid_ptr(reinterpret_cast<void*>(rc)) && ext.IsSane(16) && ext.Num() > 0)
                {
                    weapon = vt_target;
                }
            }

            if (!weapon)
            {
                std::vector<std::uintptr_t> all_weapons;
                collect_all_vehicle_weapons(vehicle, all_weapons);
                if (!all_weapons.empty())
                {
                    weapon = pick_weapon_by_camera(all_weapons, cam_yaw, cam_pitch);
                }
            }

            if (!weapon)
                return ctx;

            std::uintptr_t rot_comp = read<std::uintptr_t>(weapon + offsets::AWDVehicleWeapon::RotationComponent);
            if (!is_valid_ptr(reinterpret_cast<void*>(rot_comp)))
                return ctx;

            ctx.rot_comp = rot_comp;

            std::uintptr_t owning = read<std::uintptr_t>(weapon + offsets::AWDVehicleWeapon::OwningVehicle);
            std::uintptr_t quat_src = is_valid_ptr(reinterpret_cast<void*>(owning)) ? owning : vehicle;
            fill_vehicle_quat(quat_src, ctx);
            ctx.vehicle_actor = reinterpret_cast<void*>(vehicle);

            FRotator live = read<FRotator>(rot_comp + offsets::UWDWeaponRotationComponent::LiveWorldRotation);

            ctx.valid = true;
            return ctx;
        }

        struct VelocityTrack
        {
            std::uintptr_t actor = 0;
            FVector window_start_position{};
            FVector raw{};
            double window_start_world_seconds = 0.0;
            double last_world_seconds = 0.0;
            float last_window_seconds = 0.f;
            ULONGLONG last_seen_ms = 0;
        };

        VelocityTrack g_velocity_tracks[IPC_MAX_PLAYERS * 2]{};

        struct VelocityMeasurement
        {
            FVector raw{};
            float window_seconds = 0.f;
            bool fresh = false;
        };

        struct SnapshotFeatureFlags
        {
            bool need_bones = false;
            bool need_velocity = false;

            static SnapshotFeatureFlags from(const Config& settings) noexcept
            {
                SnapshotFeatureFlags flags{};
                flags.need_bones = settings.esp.box || settings.esp.health || settings.esp.skeleton || settings.esp.agent_name || settings.esp.distance ||
                                   settings.aimbot.enabled || settings.mortar.mortar_aim;
                flags.need_velocity = settings.prediction.enabled ||
                                      settings.aimbot.enabled || settings.mortar.mortar_aim;
                return flags;
            }
        };

        struct VelocitySample
        {
            VelocityMeasurement measured{};
            FVector engine{};
            bool engine_valid = false;

            bool measured_valid() const noexcept
            {
                return measured.window_seconds >= 0.05f && finite(measured.raw) &&
                       measured.raw.Length() <= 20000.0;
            }
        };

        VelocityMeasurement track_velocity(std::uintptr_t actor, const FVector& position, ULONGLONG now_ms, double world_seconds, bool world_timing_valid)
        {
            VelocityTrack* selected = nullptr;
            VelocityTrack* oldest = &g_velocity_tracks[0];
            for (VelocityTrack& track : g_velocity_tracks)
            {
                if (track.actor == actor)
                {
                    selected = &track;
                    break;
                }
                if (track.actor == 0 || track.last_seen_ms < oldest->last_seen_ms)
                    oldest = &track;
            }

            if (!selected)
            {
                selected = oldest;
                *selected = {};
                selected->actor = actor;
                selected->window_start_position = position;
                selected->window_start_world_seconds = world_seconds;
                selected->last_world_seconds = world_seconds;
                selected->last_seen_ms = now_ms;
                return {};
            }

            selected->last_seen_ms = now_ms;
            VelocityMeasurement result{selected->raw, selected->last_window_seconds, false};
            if (!world_timing_valid || !std::isfinite(world_seconds))
                return result;

            if (world_seconds <= selected->last_world_seconds + 0.000001)
                return result; // repeated PeekMessage call in the same world frame

            if (world_seconds < selected->last_world_seconds || world_seconds - selected->last_world_seconds > 1.0)
            {
                selected->window_start_position = position;
                selected->window_start_world_seconds = world_seconds;
                selected->last_world_seconds = world_seconds;
                selected->raw = {};
                selected->last_window_seconds = 0.f;
                return {};
            }
            selected->last_world_seconds = world_seconds;

            const double elapsed = world_seconds - selected->window_start_world_seconds;
            constexpr double minimum_window_seconds = 0.075;
            if (elapsed < minimum_window_seconds)
                return result;

            FVector measured = (position - selected->window_start_position) * (1.0 / elapsed);
            if (!finite(measured) || measured.Length() > 20000.0)
                measured = {};

            selected->raw = measured;
            selected->last_window_seconds = static_cast<float>(elapsed);
            selected->window_start_position = position;
            selected->window_start_world_seconds = world_seconds;
            return {selected->raw, selected->last_window_seconds, true};
        }

        VelocitySample sample_player_velocity(std::uintptr_t actor, const FVector& position, ULONGLONG now_ms, double world_seconds, bool world_timing_valid, bool enabled)
        {
            VelocitySample sample{};
            if (!enabled)
                return sample;

            sample.measured = track_velocity(actor, position, now_ms, world_seconds, world_timing_valid);
            sample.engine_valid =
                engine_funcs::get_velocity(reinterpret_cast<void*>(actor), sample.engine) &&
                finite(sample.engine) && sample.engine.Length() <= 20000.0;
            return sample;
        }

    } // namespace

    void init()
    {
        g_match = {};
        g_local_name[0] = L'\0';
        g_next_name_read = 0;
        extras::reset();
        tracers::reset();
        wdgs::actors::reset();
        reset_actor_object_cache();
        wdgs::snapshot::reset_caches();
        wdgs::magic_bullet::reset();
        wdgs::anti_sam::reset();
        aimbot::reset();
        wdgs::mortar_aim::reset();
        g_countermeasure_vehicle = 0;
        g_countermeasure_vehicle_is_rotary = false;
        g_countermeasure_vehicle_last_seen_ms = 0;
        for (VelocityTrack& track : g_velocity_tracks)
            track = {};
    }

    void shutdown()
    {
        init();
        engine_funcs::shutdown();
    }

    void tick(Snapshot& snapshot, const Config& settings, bool menu_visible)
    {
        game_actions::update(settings, menu_visible);
        const SnapshotFeatureFlags features = SnapshotFeatureFlags::from(settings);

        // Keep vector capacity between frames; invalid worlds never retain drawable data.
        snapshot.valid = false;
        snapshot.local_name[0] = L'\0';
        snapshot.players.clear();
        snapshot.vehicles.clear();
        snapshot.dropped_items.clear();
        snapshot.markers.clear();
        snapshot.trails.clear();

        MatchContext match;
        if (!read_match(match))
        {
            if (g_match.world)
            {
                shutdown();
                // log("match unavailable; caches cleared");
            }
            return;
        }
        if (match.world != g_match.world || match.level != g_match.level || match.controller != g_match.controller || match.pawn != g_match.pawn || match.camera != g_match.camera || match.player_state != g_match.player_state)
        {
            shutdown();
            g_match = match;
            // log("match context ready");
        }
        begin_actor_object_frame();
        auto world = reinterpret_cast<UWorld*>(match.world);
        auto level = reinterpret_cast<ULevel*>(match.level);
        auto my_controller = reinterpret_cast<APlayerController*>(match.controller);
        const auto my_pawn = match.pawn;
        const auto camera_manager_ptr = match.camera;

        if (!engine_funcs::init() || !current_match(match))
            return;

        double world_delta_seconds = 0.0;
        double world_time_seconds = 0.0;
        const bool world_timing_valid =
            engine_funcs::get_world_delta_seconds(world, world_delta_seconds) &&
            engine_funcs::get_time_seconds(world, world_time_seconds);

        CameraIPC cam{};
        void* camera_manager = reinterpret_cast<void*>(camera_manager_ptr);
        if (!read_camera_cache(camera_manager_ptr, cam) && (!engine_funcs::get_camera_location(camera_manager, cam.location) || !engine_funcs::get_camera_rotation(camera_manager, cam.rotation) || !engine_funcs::get_fov_angle(camera_manager, cam.fov)))
            return;

        if (!finite(cam.location) || !finite(cam.rotation) || !std::isfinite(cam.fov) || cam.fov <= 1.f || cam.fov >= 179.f)
            return;
        const visual_math::Projection projection(cam);

        // Keep the occupied vehicle as the scan anchor even when the class
        // probe is transiently stale during a hard yaw.  anti_sam::tick still
        // validates the native flare call as a rotary vehicle before firing;
        // the scan itself must not disappear just because that probe flickers.
        const std::uint64_t countermeasure_now = frame_ticks;
        const std::uintptr_t observed_vehicle = find_occupied_vehicle(my_pawn);
        bool countermeasure_refreshed = false;
        if (observed_vehicle)
        {
            const bool observed_is_rotary = engine_funcs::is_rotary_vehicle(reinterpret_cast<void*>(observed_vehicle));
            if (observed_is_rotary)
            {
                g_countermeasure_vehicle_last_seen_ms = countermeasure_now;
                g_countermeasure_vehicle = observed_vehicle;
                g_countermeasure_vehicle_is_rotary = true;
                countermeasure_refreshed = true;
            }
        }
        if (!countermeasure_refreshed && (!is_valid_ptr(reinterpret_cast<void*>(g_countermeasure_vehicle)) || countermeasure_now - g_countermeasure_vehicle_last_seen_ms > kCountermeasureVehicleGraceMs))
        {
            // CurrentSeat/ReplicatedSeat may briefly read null while the rotary
            // movement component updates during a hard yaw.  Preserve the last
            // verified helicopter through that transient window.
            g_countermeasure_vehicle = 0;
            g_countermeasure_vehicle_is_rotary = false;
        }
        wdgs::anti_sam::Status anti_sam_status{};
        if (g_countermeasure_vehicle && g_countermeasure_vehicle_is_rotary)
        {
            // Measure threats against the helicopter itself rather than the
            // camera. Third-person camera offsets move when the pilot yaws and
            // previously broke the distance trend for otherwise valid missiles.
            FVector countermeasure_origin{};
            bool countermeasure_origin_valid = false;
            void* vehicle_root = engine_funcs::k2_get_root_component(reinterpret_cast<void*>(g_countermeasure_vehicle));
            FTransform vehicle_transform{};
            if (vehicle_root && engine_funcs::k2_get_component_to_world(vehicle_root, vehicle_transform) && finite(vehicle_transform.Translation))
            {
                countermeasure_origin = vehicle_transform.Translation;
                countermeasure_origin_valid = true;
            }
            // If a blueprint swaps/rebuilds its vehicle root during a hard
            // rotation, use the seated pawn as the stable secondary anchor.
            // This avoids falling back directly to the third-person camera,
            // whose position changes with view rotation on the Little Bird.
            if (!countermeasure_origin_valid)
            {
                void* pawn_root = engine_funcs::k2_get_root_component(reinterpret_cast<void*>(my_pawn));
                FTransform pawn_transform{};
                if (pawn_root && engine_funcs::k2_get_component_to_world(pawn_root, pawn_transform) && finite(pawn_transform.Translation))
                {
                    countermeasure_origin = pawn_transform.Translation;
                    countermeasure_origin_valid = true;
                }
            }
            if (!countermeasure_origin_valid)
                countermeasure_origin = cam.location;
            anti_sam_status = wdgs::anti_sam::tick(reinterpret_cast<void*>(g_countermeasure_vehicle), countermeasure_origin, settings.anti_sam.auto_flare, settings.anti_sam.flare_warning);
        }

        const auto my_player_state = read<std::uintptr_t>(my_pawn + offsets::APawn::PlayerState);

        void* local_faction = is_valid_ptr(reinterpret_cast<void*>(my_player_state))
                                  ? wdgs::snapshot::GetFaction(my_player_state)
                                  : nullptr;

        const auto actors = level->Actors();
        constexpr int kMaxActors = 262144;
        if (!actors.IsSane(kMaxActors) || actors.Count <= 0)
            return;

        wdgs::actors::resolve_names();

        const std::uint32_t player_name_index =
            wdgs::actors::comparison_index(wdgs::actors::Kind::player);

        const wdgs::snapshot::ActorSnapshotContext context{
            static_cast<std::int32_t>(player_name_index),
            cam.location,
            local_faction};

        extras::begin(match.world, settings);
        Snapshot& output = snapshot;
        output.world = match.world;
        output.controller = match.controller;
        output.pawn = match.pawn;
        output.camera_manager = match.camera;
        output.time = frame_time;
        output.markers.reserve(128);
        output.camera = cam;
        output.anti_sam = anti_sam_status;
        output.local_yaw = 0.f;
        output.local_yaw_valid = false;
        if (settings.esp.minimap)
        {
            output.local_yaw_valid = read_local_yaw(my_pawn, output.local_yaw);
            if (frame_ticks >= g_next_name_read)
            {
                const auto name = reinterpret_cast<APlayerState*>(match.player_state)->PlayerName();
                wcsncpy_s(g_local_name, name.c_str(), _TRUNCATE);
                g_next_name_read = frame_ticks + 250;
            }
            wcsncpy_s(output.local_name, g_local_name, _TRUNCATE);
        }
        output.prediction_line = {};
        output.aim_selected_actor = 0;

        output.weapon_stats = {};
        output.weapon_stats_valid = false;

        constexpr float fallback_bullet_speed = 46300.f;
        float local_bullet_speed = fallback_bullet_speed;
        float local_zeroing_meters = 100.f;
        float local_gravity_scale = 1.f;
        const auto local_mesh = read<std::uintptr_t>(my_pawn + offsets::ACharacter::Mesh);
        if (is_valid_ptr(reinterpret_cast<void*>(local_mesh)))
        {
            void* anim_instance = engine_funcs::get_anim_instance(reinterpret_cast<void*>(local_mesh));
            wdgs::weapon_stats::Snapshot weapon{};
            output.weapon_stats_valid = wdgs::weapon_stats::try_read(anim_instance, weapon);
            if (output.weapon_stats_valid)
                output.weapon_stats = weapon;
            if (output.weapon_stats_valid)
            {
                if (std::isfinite(weapon.muzzle_velocity) && weapon.muzzle_velocity >= 1000.f && weapon.muzzle_velocity <= 300000.f)
                {
                    local_bullet_speed = weapon.muzzle_velocity;
                }
                else if (std::isfinite(weapon.typical_speed) && weapon.typical_speed >= 1000.f && weapon.typical_speed <= 300000.f)
                {
                    local_bullet_speed = weapon.typical_speed;
                }
                if (std::isfinite(weapon.gravity_scale_guess) && weapon.gravity_scale_guess > 0.01f && weapon.gravity_scale_guess <= 3.f)
                    local_gravity_scale = weapon.gravity_scale_guess;
            }
        }
        if (!std::isfinite(local_bullet_speed) || local_bullet_speed < 1000.f || local_bullet_speed > 300000.f)
            local_bullet_speed = fallback_bullet_speed;

        if (output.weapon_stats_valid)
        {
            FVector muzzle{};
            output.weapon_stats.muzzle_valid =
                wdgs::weapon_stats::try_read_muzzle(my_pawn, muzzle);
            if (output.weapon_stats.muzzle_valid)
                output.weapon_stats.muzzle_position = muzzle;
        }

        extras::recoil(output.weapon_stats, settings.extra.no_recoil);
        output.mortar = {};
        wdgs::mortar_aim::try_read(my_pawn, camera_manager_ptr, output.mortar);

        output.players.clear();
        output.players.reserve(100);
        output.vehicles.clear();
        output.vehicles.reserve(16);
        output.dropped_items.clear();
        output.dropped_items.reserve(32);

        wdgs::snapshot::begin_frame();
        const ULONGLONG velocity_timestamp = frame_ticks;
        const int viewport_width = screen_width;
        const int viewport_height = screen_height;
        const bool need_player_screen = settings.esp.box || settings.esp.skeleton ||
                                        settings.esp.lines || settings.esp.health || settings.esp.agent_name ||
                                        settings.esp.distance || settings.aimbot.enabled ||
                                        (output.mortar.valid && settings.mortar.mortar_aim);
        int nearest_bone_budget = 8;
        const float radar_range = radar_scan_range(settings);
        const bool aim_vehicles = settings.aimbot.enabled && (settings.aimbot.silent_aim || settings.aimbot.magic_bullet);
        const bool scan_vehicles = settings.esp.vehicles || aim_vehicles || (settings.esp.minimap && (settings.radar.helicopters || settings.radar.ground || settings.radar.boats || settings.radar.stationary));
        for (int i = 0; i < actors.Count; ++i)
        {
            if ((i & 15) == 0 && !current_match(match))
                return;
            std::uintptr_t actor = 0;
            if (!actors.TryGet(i, actor, kMaxActors) || actor == my_pawn || !live(actor))
                continue;

            extras::collect(actor, cam, settings, output);
            const FNameValue actor_name = read<FNameValue>(actor + offsets::UObject::NamePrivate);
            const wdgs::actors::Match effective_match = resolve_actor_match(actor, actor_name, scan_vehicles);

            if (effective_match.kind == wdgs::actors::Kind::dropped_item || wdgs::actors::is_vehicle(effective_match.kind))
            {
                float actor_distance_limit = 0.f;
                if (effective_match.kind == wdgs::actors::Kind::dropped_item)
                {
                    if (settings.esp.loot)
                        actor_distance_limit = settings.esp.loot_distance;
                    if (settings.radar.items)
                        actor_distance_limit = std::max(actor_distance_limit, radar_range);
                }
                else
                {
                    if (settings.esp.vehicles || aim_vehicles)
                        actor_distance_limit = settings.esp.vehicle_distance;
                    if (radar_vehicle_visible(settings, effective_match.kind))
                        actor_distance_limit = std::max(actor_distance_limit, radar_range);
                }

                wdgs::world_actors::Snapshot world_actor{};
                if (actor_distance_limit > 0.f && wdgs::world_actors::try_build(actor, cam.location, actor_name, effective_match, actor_distance_limit, world_actor))
                {
                    ProjectedWorldActor projected_world{};
                    projected_world.actor = world_actor;
                    projected_world.screen = projection.project(world_actor.world_position);
                    // Keep the world-space record even when the root is outside
                    // the viewport so the radar still receives off-screen actors.
                    if (effective_match.kind == wdgs::actors::Kind::dropped_item && output.dropped_items.size() < wdgs::world_actors::max_dropped_items)
                        output.dropped_items.push_back(projected_world);
                    else if (wdgs::actors::is_vehicle(effective_match.kind) && output.vehicles.size() < wdgs::world_actors::max_vehicles)
                        output.vehicles.push_back(projected_world);
                }
            }

            // Do not make the whole player pass depend on one resolved FName.
            // During lobby/map transitions the name pool can lag behind the
            // actor array.  PlayerState::PawnPrivate is authoritative and
            // remains valid for downed/revivable players, while surrendered
            // actors lose the link and naturally disappear from ESP.
            bool player_candidate = effective_match.kind == wdgs::actors::Kind::player;
            if (!player_candidate && effective_match.kind == wdgs::actors::Kind::none)
            {
                player_candidate = g_verified_player_names.count(actor_name.ComparisonIndex) != 0;
                if (!player_candidate && has_verified_player_state_link(actor))
                {
                    player_candidate = true;
                    if (actor_name.ComparisonIndex != 0)
                        g_verified_player_names.insert(actor_name.ComparisonIndex);
                }
            }
            if (!player_candidate)
                continue;

            Player player{};
            std::uintptr_t player_root_component = 0;
            if (!wdgs::snapshot::TryBuildPlayer(actor, context, player, player_root_component))
                continue;

            ProjectedPlayer projected{};
            const bool needs_offscreen_target_data =
                output.mortar.valid && settings.mortar.mortar_aim;
            const auto root_point = need_player_screen ? projection.project(player.world_pos) : ScreenPoint{};
            const FVector2D root_screen{root_point.x, root_point.y};
            const bool root_projected = root_point.valid;
            const bool root_on_screen = root_projected &&
                                        std::isfinite(root_screen.X) && std::isfinite(root_screen.Y) &&
                                        root_screen.X > 0.0 && root_screen.Y > 0.0 &&
                                        root_screen.X < static_cast<double>(viewport_width) &&
                                        root_screen.Y < static_cast<double>(viewport_height);
            if (root_on_screen)
            {
                projected.screen.x = static_cast<float>(root_screen.X);
                projected.screen.y = static_cast<float>(root_screen.Y);
                projected.screen.valid = true;
            }

            // Bone sockets, line-of-sight and velocity are only useful for an
            // actor that can participate in the current screen-space target or
            // mortar path. Keep the lightweight world record for the radar.
            const bool needs_target_data = root_on_screen || needs_offscreen_target_data;
            if (features.need_bones && needs_target_data)
            {
                const float aim_margin = settings.aimbot.fov + std::clamp(4200.f / std::max(player.distance, 1.f), 20.f, 250.f);
                const bool nearest = settings.aimbot.enabled && settings.aimbot.bone == 4 && std::fabs(root_screen.X - screen_width * 0.5) < aim_margin && std::fabs(root_screen.Y - screen_height * 0.5) < aim_margin;
                const bool drawn_skeleton = root_on_screen && settings.esp.skeleton && player.distance <= settings.esp.skeleton_distance;
                const bool extra_bones = root_on_screen && nearest && !drawn_skeleton && nearest_bone_budget > 0;
                if (extra_bones)
                    --nearest_bone_budget;
                const bool full_skeleton = drawn_skeleton || extra_bones;
                wdgs::snapshot::PopulatePlayerBones(actor, player, full_skeleton, settings.esp.box || settings.esp.health);
            }
            if (needs_target_data)
            {
                if (settings.esp.visible_check || (settings.aimbot.enabled && settings.aimbot.visible_check))
                    player.isVisible = engine_funcs::line_of_sight_to(my_controller, reinterpret_cast<void*>(actor));
                wdgs::snapshot::PopulatePlayerMortarState(actor, player_root_component, player);
            }
            const VelocitySample velocity = sample_player_velocity(actor, player.world_pos, velocity_timestamp, world_time_seconds, world_timing_valid, features.need_velocity && needs_target_data);
            projected.player = player;
            // Remote Actor::GetVelocity can describe the replicated movement intent rather
            // than the displacement actually rendered by this client.  Once the validated
            // world-time window is populated, predict from its latest displacement.  The
            // window already suppresses frame noise; a second EMA trails direction changes.
            if (world_timing_valid && velocity.measured_valid())
            {
                projected.player.velocity = velocity.measured.raw;
            }
            else if (velocity.engine_valid)
            {
                projected.player.velocity = velocity.engine;
            }
            else
            {
                projected.player.velocity = {};
            }
            projected.actor_addr = actor;

            if (root_on_screen && player.has_bones)
            {
                // Boxes and health bars need head/root even with skeleton/aim off.
                const bool project_full_skeleton = settings.esp.skeleton &&
                                                   player.distance <= settings.esp.skeleton_distance;
                for (int bone_index = 0; bone_index < BONE_COUNT; ++bone_index)
                {
                    if (!project_full_skeleton && !settings.aimbot.enabled && bone_index != BONE_CHEST && bone_index != BONE_HEAD && bone_index != BONE_ROOT)
                        continue;
                    const auto& bone = player.bones[bone_index];
                    if (!finite(bone) || (bone.X == 0.0 && bone.Y == 0.0 && bone.Z == 0.0))
                        continue;
                    const auto point = projection.project(bone);
                    if (point.valid && point.x >= 0 && point.y >= 0 && point.x < viewport_width && point.y < viewport_height)
                        projected.bones[bone_index] = point;
                }
            }

            output.players.push_back(projected);
        }
        extras::finish(cam, settings, output);
        // Vehicle target conversion is only needed by the aimbot/mortar paths.
        // Keep world-actor rendering independent so disabled targeting features do
        // not allocate/copy a second representation of every vehicle each tick.
        aimbot::VehicleAimContext vehicle_ctx{};
        if (settings.aimbot.enabled)
        {
            vehicle_ctx = find_vehicle_aim_context(my_pawn, camera_manager_ptr, cam.rotation);
            if (!vehicle_ctx.vehicle_actor)
                vehicle_ctx.vehicle_actor = reinterpret_cast<void*>(observed_vehicle);
        }

        if (settings.aimbot.enabled || (output.mortar.valid && settings.mortar.mortar_aim))
        {
            for (const auto& v : output.vehicles)
            {
                if (!v.screen.valid || v.actor.distance_meters > settings.esp.vehicle_distance)
                    continue;
                ProjectedPlayer vpp{};
                vpp.player.world_pos = v.actor.world_position;
                vpp.player.distance = v.actor.distance_meters;
                vpp.player.health = 10000.f;
                vpp.player.max_health = 10000.f;
                vpp.player.has_bones = true;
                for (int b = 0; b < BONE_COUNT; b++)
                    vpp.player.bones[b] = v.actor.world_position;
                vpp.screen = v.screen;
                for (int b = 0; b < BONE_COUNT; b++)
                    vpp.bones[b] = v.screen;
                vpp.actor_addr = v.actor.address;
                vpp.is_vehicle = true;
                vpp.team_known = extras::vehicle_team(v.actor.address, local_faction, vpp.player.is_in_team);
                vpp.player.isVisible = !settings.aimbot.visible_check || engine_funcs::line_of_sight_to(my_controller, reinterpret_cast<void*>(v.actor.address));
                output.players.push_back(vpp);
            }
        }

        if (!current_match(match))
            return;
        if (output.mortar.valid && (settings.mortar.mortar_aim || settings.extra.mortar_mode == 1))
        {
            if (settings.extra.mortar_mode == 0)
            {
                bool aim_key_down = !menu_visible && (GetAsyncKeyState(settings.aimbot.key) & 0x8000) != 0;
                std::vector<wdgs::mortar_aim::TargetPlayer> mortar_targets;
                mortar_targets.reserve(output.players.size());
                for (const auto& pp : output.players)
                {
                    if (!std::isfinite(pp.player.health) || pp.player.health <= 0.f)
                        continue;
                    wdgs::mortar_aim::TargetPlayer tp{};
                    // Vehicle actors do not expose the infantry bone array.  Use the
                    // actor world origin for them; infantry keeps the pelvis point.
                    // This also lets the marker-driven indirect solver select a vehicle
                    // target instead of silently reporting zero candidates.
                    tp.position = pp.is_vehicle
                                      ? pp.player.world_pos
                                      : pp.player.bones[BONE_PELVIS];
                    tp.velocity = pp.player.velocity;
                    tp.actor_addr = pp.actor_addr;
                    tp.is_team = pp.player.is_in_team;
                    tp.health = pp.player.health;
                    wcsncpy_s(tp.name, pp.player.player_name, _TRUNCATE);
                    mortar_targets.push_back(tp);
                }
                wdgs::mortar_aim::auto_target(output.mortar, mortar_targets, aim_key_down, settings.aimbot.team_check, settings.aimbot.smooth, settings.mortar.fov, settings.mortar.range_scale, settings.mortar.arc_mode, menu_visible);
            }
            else
                aimbot::reset();
        }
        else
        {
            const std::uint32_t local_pawn_internal_index = read<std::uint32_t>(my_pawn + offsets::UObject::InternalIndex);
            const std::uint32_t local_vehicle_internal_index =
                is_valid_ptr(reinterpret_cast<void*>(observed_vehicle))
                    ? read<std::uint32_t>(observed_vehicle + offsets::UObject::InternalIndex)
                    : 0;
            aimbot::tick(output.players, my_controller, cam, settings.aimbot, settings.prediction, local_bullet_speed, local_zeroing_meters, local_gravity_scale, output.prediction_line, output.weapon_stats.muzzle_position, output.weapon_stats.muzzle_valid, vehicle_ctx, local_pawn_internal_index, local_vehicle_internal_index, menu_visible, settings.magic_ignore_visibility, settings.magic_min_distance);
            output.aim_selected_actor = aimbot::selected_actor();
            // With both visibility filters off, trace only the selected player for its highlight.
            if (output.aim_selected_actor && settings.esp.enabled && !settings.esp.visible_check && !settings.aimbot.visible_check)
                for (auto& p : output.players)
                    if (p.actor_addr == output.aim_selected_actor && !p.is_vehicle)
                    {
                        p.player.isVisible = engine_funcs::line_of_sight_to(my_controller, reinterpret_cast<void*>(p.actor_addr));
                        break;
                    }
        }
        if (!current_match(match))
            return;
        tracers::tick(output, read<std::uint32_t>(my_pawn + offsets::UObject::InternalIndex), observed_vehicle ? read<std::uint32_t>(observed_vehicle + offsets::UObject::InternalIndex) : 0, settings.extra.tracers, settings.extra.tracer_lifetime);
        output.valid = current_match(match);
        game_actions::copy_target_name(output);
    }

    bool camera_for_render(const Snapshot& snapshot, CameraIPC& camera)
    {
        if (!snapshot.valid || !snapshot.world || !snapshot.camera_manager || read<std::uintptr_t>(offsets::base + offsets::UWorldPtr) != snapshot.world || !live(snapshot.world) || !live(snapshot.controller) || !live(snapshot.pawn) || !live(snapshot.camera_manager) || read<std::uintptr_t>(snapshot.controller + offsets::APlayerController_Extra::ControllerPawn) != snapshot.pawn || read<std::uintptr_t>(snapshot.controller + offsets::APlayerController::PlayerCameraManager) != snapshot.camera_manager)
            return false;
        camera = snapshot.camera;
        read_camera_cache(snapshot.camera_manager, camera);
        return true;
    }
} // namespace game
