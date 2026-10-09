#include "stdafx.h"
#include "aimbot.h"
#include "engine_funcs.h"
#include "config.h"
#include "classes.h"
#include "prediction.h"
#include "magic_bullet.h"
#include "offsets.h"
#include <cmath>

namespace
{

    double g_last_tick{};
    std::uintptr_t g_locked_actor = 0;
    std::uintptr_t g_magic_dead_actor = 0;
    double g_magic_dead_since{};
    bool g_magic_reported = false;
    bool g_magic_reported_target = false;
    constexpr float kMagicShortRangeMeters = 50.f;
    // Helpers
    int bone_slot_for_config(int cfg_bone)
    {
        switch (cfg_bone)
        {
        case 0:
            return BONE_HEAD;
        case 1:
            return BONE_NECK;
        case 2:
            return BONE_CHEST;
        case 3:
            return BONE_PELVIS;
        default:
            return BONE_HEAD;
        }
    }

    float distance_to_crosshair(float x, float y)
    {
        float cx = static_cast<float>(screen_width) * 0.5f;
        float cy = static_cast<float>(screen_height) * 0.5f;
        float dx = x - cx;
        float dy = y - cy;
        return std::sqrtf(dx * dx + dy * dy);
    }
    // Aim methods
    void aim_turret(void* controller, const FVector& aim_origin, const FVector& aim_point, float delta_time, float smooth, const aimbot::VehicleAimContext& veh)
    {
        FRotator mount_rot = read<FRotator>(veh.rot_comp + offsets::UWDWeaponRotationComponent::TargetRotation);

        FRotator current_rot;
        current_rot.Pitch = mount_rot.Pitch + veh.vehicle_pitch;
        current_rot.Yaw = mount_rot.Yaw + veh.vehicle_yaw;
        current_rot.Roll = 0.0;

        FRotator target_rot = engine_funcs::find_look_at_rotation(aim_origin, aim_point);
        FRotator final_rot = engine_funcs::rinterp_to(current_rot, target_rot, delta_time, smooth);

        FRotator write_rot;
        write_rot.Pitch = final_rot.Pitch - veh.vehicle_pitch;
        float yaw_diff = final_rot.Yaw - veh.vehicle_yaw;
        while (yaw_diff > 180.0)
            yaw_diff -= 360.0;
        while (yaw_diff < -180.0)
            yaw_diff += 360.0;
        write_rot.Yaw = yaw_diff;
        write_rot.Roll = mount_rot.Roll;

        write<FRotator>(veh.rot_comp + offsets::UWDWeaponRotationComponent::TargetRotation, write_rot);

        // Vehicle weapons keep their barrel rotation in the weapon's local
        // RotationComponent, while the player camera still follows the
        // controller's world rotation.  Writing only the local turret field
        // leaves helicopter/tank view direction behind the barrel.  Apply the
        // same world-space solution to the controller as the vehicle aimbot
        // implementation, so both states move together on the same tick.
        engine_funcs::set_control_rotation(controller, final_rot);
    }

    void aim_on_foot(void* controller, const FVector& aim_origin, const FVector& aim_point, float delta_time, float smooth)
    {
        FRotator current_rot = engine_funcs::get_control_rotation(controller);
        FRotator target_rot = engine_funcs::find_look_at_rotation(aim_origin, aim_point);
        FRotator final_rot = engine_funcs::rinterp_to(current_rot, target_rot, delta_time, smooth);
        final_rot.Roll = 0.0;
        engine_funcs::set_control_rotation(controller, final_rot);
    }

} // namespace
int aimbot::target_bone(const game::ProjectedPlayer& player, int configured)
{
    if (configured != 4)
    {
        const int slot = bone_slot_for_config(configured);
        return player.bones[slot].valid ? slot : -1;
    }
    int best = -1;
    float distance = (std::numeric_limits<float>::max)();
    for (int i = 0; i < BONE_ROOT; ++i)
    {
        const auto& point = player.bones[i];
        if (!point.valid)
            continue;
        const float d = distance_to_crosshair(point.x, point.y);
        if (std::isfinite(d) && d < distance)
        {
            distance = d;
            best = i;
        }
    }
    return best;
}

void aimbot::reset()
{
    g_magic_reported = false;
    g_magic_reported_target = false;
    g_locked_actor = 0;
    g_magic_dead_actor = 0;
    g_magic_dead_since = {};
    g_last_tick = {};
}

// Main tick
void aimbot::tick(const std::vector<game::ProjectedPlayer>& players, void* controller, const CameraIPC& camera, const AimbotSettings& settings, const PredictionSettings& prediction_settings, float local_bullet_speed, float local_zeroing_meters, float local_gravity_scale, game::PredictionLine& prediction_line, const FVector& muzzle_position, bool muzzle_valid, const VehicleAimContext& vehicle_aim, std::uint32_t local_pawn_internal_index, std::uint32_t local_vehicle_internal_index, bool menu_visible, bool magic_ignore_visibility)
{
    prediction_line = {};
    if (!settings.enabled || !is_valid_ptr(controller))
    {
        reset();
        return;
    }

    bool key_down = !menu_visible && (GetAsyncKeyState(settings.key) & 0x8000) != 0;
    if (!key_down)
    {
        reset();
        return;
    }
    const bool report = settings.magic_bullet && !g_magic_reported;
    g_magic_reported = settings.magic_bullet;
    if (!settings.magic_bullet)
        g_magic_reported_target = false;

    const double now = frame_time;
    float delta_time = static_cast<float>(now - g_last_tick);
    g_last_tick = now;
    if (delta_time <= 0.f || delta_time > 0.1f)
        delta_time = 0.016f;

    int aim_bone = bone_slot_for_config(settings.bone);
    float aim_fov = settings.fov;
    if (aim_bone < 0 || aim_bone >= BONE_COUNT || !std::isfinite(aim_fov) || aim_fov <= 0.f)
    {
        if (report)
            log("Magic: invalid bone or FOV setting");
        return;
    }

    if (settings.mode < 0 || settings.mode > 2)
    {
        if (report)
            log("Magic: invalid target mode");
        return;
    }
    // Target selection
    auto valid_target = [&](const game::ProjectedPlayer& pp, bool require_fov)
    {
        const int slot = target_bone(pp, settings.bone);
        if (slot < 0 || pp.actor_addr == reinterpret_cast<std::uintptr_t>(vehicle_aim.vehicle_actor) || (pp.is_vehicle && (!pp.team_known || (!settings.silent_aim && !settings.magic_bullet))) || (settings.team_check && pp.player.is_in_team) || !pp.player.has_bones || !pp.bones[slot].valid || !std::isfinite(pp.player.health) || pp.player.health <= 0.f || !std::isfinite(pp.player.distance) || pp.player.distance < 0.f)
            return false;

        if (!require_fov)
            return true;

        const float dist_cross = distance_to_crosshair(pp.bones[slot].x, pp.bones[slot].y);
        return std::isfinite(dist_cross) && dist_cross <= aim_fov;
    };

    const game::ProjectedPlayer* best = nullptr;

    if (g_locked_actor != 0)
    {
        const game::ProjectedPlayer* locked = nullptr;
        for (const auto& pp : players)
        {
            if (pp.actor_addr == g_locked_actor)
            {
                locked = &pp;
                break;
            }
        }

        const bool dead_locked_target = locked && !locked->is_vehicle &&
                                        std::isfinite(locked->player.health) && locked->player.health <= 0.f;
        const float delay_off = (std::clamp)(settings.magic_bullet_delay_off, 0.f, 10.f);

        if (settings.magic_bullet && dead_locked_target && delay_off > 0.f)
        {
            if (g_magic_dead_actor != g_locked_actor || g_magic_dead_since == 0)
            {
                g_magic_dead_actor = g_locked_actor;
                g_magic_dead_since = now;
            }

            const float elapsed = static_cast<float>(now - g_magic_dead_since);
            if (elapsed < delay_off)
            {
                if (report)
                    log("Magic: waiting for dead-target delay");
                return;
            }
        }
        else
        {
            g_magic_dead_actor = 0;
            g_magic_dead_since = {};
        }

        if (locked && valid_target(*locked, false))
            best = locked;

        if (!best)
        {
            // The locked actor may have entered a vehicle or otherwise become
            // invalid. Drop the lock and continue so a player target can be
            // selected in this same frame.
            g_locked_actor = 0;
            g_magic_dead_actor = 0;
            g_magic_dead_since = {};
        }
    }

    float best_dist_cross = (std::numeric_limits<float>::max)();
    float best_dist_3d = (std::numeric_limits<float>::max)();
    float best_health = (std::numeric_limits<float>::max)();

    if (!best)
    {
        for (const auto& pp : players)
        {
            if (!valid_target(pp, true))
                continue;

            const int slot = target_bone(pp, settings.bone);
            const float dist_cross = distance_to_crosshair(pp.bones[slot].x, pp.bones[slot].y);
            if (pp.is_vehicle && best && !best->is_vehicle)
                continue;
            bool replace = false;

            switch (settings.mode)
            {
            case 0:
                replace = pp.player.distance < best_dist_3d ||
                          (pp.player.distance == best_dist_3d && dist_cross < best_dist_cross);
                break;
            case 1:
                replace = dist_cross < best_dist_cross;
                break;
            case 2:
                replace = pp.player.health < best_health ||
                          (pp.player.health == best_health && dist_cross < best_dist_cross);
                break;
            }

            replace = replace || (best && best->is_vehicle && !pp.is_vehicle);
            if (replace)
            {
                best = &pp;
                best_dist_cross = dist_cross;
                best_dist_3d = pp.player.distance;
                best_health = pp.player.health;
            }
        }

        if (!best)
        {
            if (report)
                log("Magic: no eligible target in FOV (players=%zu fov=%.0f bone=%d)", players.size(), aim_fov, settings.bone);
            if (settings.magic_bullet)
                wdgs::magic_bullet::probe(camera.location);
            return;
        }

        g_locked_actor = best->actor_addr;
    }

    // Visibility is sampled once while building the frame snapshot. Repeating
    // the engine line trace here adds a second expensive ProcessEvent call for
    // the same target on every aim tick.
    if (settings.visible_check && !vehicle_aim.valid && !best->player.isVisible && !(settings.magic_bullet && magic_ignore_visibility))
    {
        if (report)
            log("Magic: selected target blocked by Aim visibility check");
        if (settings.magic_bullet)
            wdgs::magic_bullet::probe(camera.location);
        return;
    }
    aim_bone = target_bone(*best, settings.bone);
    if (aim_bone < 0)
    {
        if (report)
            log("Magic: selected target has no projected bone");
        return;
    }
    // Aim point & prediction
    const FVector& bone_pos = best->player.bones[aim_bone];
    if (bone_pos.X == 0.0 && bone_pos.Y == 0.0 && bone_pos.Z == 0.0)
    {
        if (report)
            log("Magic: selected target has no bone position");
        if (settings.magic_bullet)
            wdgs::magic_bullet::probe(camera.location);
        return;
    }

    const FVector aim_origin = muzzle_valid ? muzzle_position : camera.location;

    FVector aim_point = bone_pos;
    // Magic Bullet retargets the live projectile directly.  Do not run the
    // ordinary prediction solver in that mode: both gravity compensation and
    // target-velocity lead would move the retarget point away from the raw
    // selected bone.
    if (!settings.magic_bullet && prediction_settings.enabled && std::isfinite(local_bullet_speed) && local_bullet_speed >= 1000.f)
    {
        prediction::Input input{};
        input.camera = aim_origin;
        input.target = bone_pos;
        input.target_velocity = best->player.velocity;
        input.bullet_speed = local_bullet_speed;
        input.zeroing_meters = local_zeroing_meters;
        input.gravity_scale = local_gravity_scale;
        input.velocity_lead = prediction_settings.velocity_lead;
        input.bullet_drop = prediction_settings.bullet_drop;

        const prediction::Result result = prediction::solve(input);
        if (result.valid)
        {
            aim_point = result.aim_point;
            if (prediction_settings.show_line && best->bones[aim_bone].valid)
            {
                FVector2D predicted_screen{};
                if (engine_funcs::project_world_to_screen(controller, aim_point, predicted_screen) && std::isfinite(predicted_screen.X) && std::isfinite(predicted_screen.Y))
                {
                    prediction_line.bone = best->bones[aim_bone];
                    prediction_line.bone_world = bone_pos;
                    prediction_line.aim_world = aim_point;
                    prediction_line.aim.x = static_cast<float>(predicted_screen.X);
                    prediction_line.aim.y = static_cast<float>(predicted_screen.Y);
                    prediction_line.aim.valid = true;
                    prediction_line.actor_addr = best->actor_addr;
                    prediction_line.valid = true;
                }
            }
        }
    }

    if (settings.magic_bullet || settings.silent_aim)
    {
        // At short range, muzzle-based prediction can overshoot because the
        // projectile is already in flight. Keep the normal predicted point at
        // range, but use the current bone position for close targets.
        const FVector& magic_target =
            (std::isfinite(best->player.distance) && best->player.distance < kMagicShortRangeMeters)
                ? bone_pos
                : aim_point;
        // Report the first redirect attempt even if selection became valid after the key press.
        const bool report_target = settings.magic_bullet && !g_magic_reported_target;
        g_magic_reported_target = settings.magic_bullet;
        if (report_target)
            log("Magic: target=%p bone=%d visible=%d on update thread", reinterpret_cast<void*>(best->actor_addr), aim_bone, best->player.isVisible);
        wdgs::magic_bullet::retarget_all(magic_target, camera.location, local_pawn_internal_index, local_vehicle_internal_index, !settings.magic_bullet, report_target);
    }

    // Silent aim redirects a new local round once; Magic Bullet keeps steering it.
    if (settings.magic_bullet || settings.silent_aim)
        return;
    // Dispatch to the correct aim method
    float smooth = static_cast<float>(settings.smooth);

    if (vehicle_aim.valid)
        aim_turret(controller, aim_origin, aim_point, delta_time, smooth, vehicle_aim);
    else
        aim_on_foot(controller, aim_origin, aim_point, delta_time, smooth);
}

std::uintptr_t aimbot::selected_actor()
{
    return g_locked_actor;
}
