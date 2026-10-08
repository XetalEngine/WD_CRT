#pragma once
#include "game.h"
struct AimbotSettings;
struct PredictionSettings;

namespace aimbot
{
    void reset();

    struct VehicleAimContext
    {
        std::uintptr_t rot_comp = 0;
        void* vehicle_actor = nullptr;
        float vehicle_pitch = 0.f;
        float vehicle_yaw = 0.f;
        float vehicle_roll = 0.f;
        bool valid = false;
    };

    void tick(const std::vector<game::ProjectedPlayer>& players, void* controller, const CameraIPC& camera, const AimbotSettings& settings, const PredictionSettings& prediction_settings, float local_bullet_speed, float local_zeroing_meters, float local_gravity_scale, game::PredictionLine& prediction_line, const FVector& muzzle_position, bool muzzle_valid, const VehicleAimContext& vehicle_aim, std::uint32_t local_pawn_internal_index, std::uint32_t local_vehicle_internal_index, bool menu_visible);

    // Actor currently selected while the configured aim key is held. The value is
    // zero immediately after key release or when no player target is available.
    std::uintptr_t selected_actor();

} // namespace aimbot
