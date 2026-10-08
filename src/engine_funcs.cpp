#include "stdafx.h"
#include "engine_funcs.h"
#include "hook_process_event.h"
#include <algorithm>
#include <cmath>

namespace
{

    void* g_default_gameplay_statics = nullptr;
    void* g_default_subsystem_blueprint_library = nullptr;
    void* g_fn_get_player_controller = nullptr;
    void* g_fn_get_player_camera_manager = nullptr;
    void* g_fn_get_player_state = nullptr;
    void* g_fn_get_world_delta_seconds = nullptr;
    void* g_fn_get_time_seconds = nullptr;
    void* g_fn_get_world_subsystem = nullptr;
    void* g_fn_get_air_density = nullptr;
    void* g_fn_get_camera_location = nullptr;
    void* g_fn_get_camera_rotation = nullptr;
    void* g_fn_get_fov_angle = nullptr;
    void* g_fn_k2_get_pawn = nullptr;
    void* g_fn_k2_get_root_component = nullptr;
    void* g_fn_get_velocity = nullptr;
    void* g_fn_k2_get_component_to_world = nullptr;
    void* g_fn_get_anim_instance = nullptr;
    void* g_default_kismet_string_library = nullptr;
    void* g_fn_conv_string_to_name = nullptr;
    void* g_fn_conv_name_to_string = nullptr;
    void* g_fn_line_of_sight = nullptr;
    void* g_fn_project_world = nullptr;
    void* g_fn_get_current_health = nullptr;
    void* g_fn_get_max_health = nullptr;
    void* g_fn_get_bone_name = nullptr;
    void* g_fn_get_socket_location = nullptr;
    void* g_fn_get_socket_rotation = nullptr;
    void* g_fn_get_all_socket_names = nullptr;
    void* g_fn_get_faction = nullptr;
    void* g_fn_get_vitality_component = nullptr;
    void* g_fn_get_weapon_behavior_component = nullptr;
    void* g_fn_get_weapon_stats = nullptr;
    void* g_fn_get_weapon_component = nullptr;
    void* g_fn_get_projectile_data = nullptr;
    void* g_fn_get_projectile_stats = nullptr;
    void* g_default_wd_artillery_range_library = nullptr;
    void* g_fn_simulate_impact_distance = nullptr;
    void* g_fn_set_control_rotation = nullptr;
    void* g_fn_get_control_rotation = nullptr;
    void* g_default_kismet_math_library = nullptr;
    void* g_fn_rinterp_to = nullptr;
    void* g_fn_find_look_at_rotation = nullptr;
    void* g_fn_k2_set_actor_rotation = nullptr;
    void* g_fn_add_yaw_input = nullptr;
    void* g_fn_add_pitch_input = nullptr;
    void* g_fn_object_is_a = nullptr;
    void* g_uclass_modular_vehicle = nullptr;
    void* g_uclass_wd_rotary_vehicle = nullptr;
    void* g_uclass_wd_airplane_vehicle = nullptr;
    void* g_uclass_wd_weather_subsystem = nullptr;
    void* g_fn_input_fire_flares = nullptr;
    bool g_functions_ready = false;
    ULONGLONG g_last_resolve_attempt = 0;

    struct GetLocalPlayerObjectParams
    {
        void* WorldContextObject;
        std::int32_t PlayerIndex;
        void* ReturnValue;
    };

    struct GetPawnParams
    {
        void* ReturnValue;
    };

    struct WorldDoubleParams
    {
        void* WorldContextObject;
        double ReturnValue;
    };

    struct GetWorldSubsystemParams
    {
        void* WorldContextObject;
        void* Class;
        void* ReturnValue;
    };

    static_assert(offsetof(GetWorldSubsystemParams, Class) == 0x8);
    static_assert(offsetof(GetWorldSubsystemParams, ReturnValue) == 0x10);
    static_assert(sizeof(GetWorldSubsystemParams) == 0x18);

    struct GetAirDensityParams
    {
        float ReturnValue;
    };

    static_assert(offsetof(WorldDoubleParams, ReturnValue) == 0x8);
    static_assert(sizeof(WorldDoubleParams) == 0x10);

    struct GetRootComponentParams
    {
        void* ReturnValue;
    };

    struct GetVelocityParams
    {
        FVector ReturnValue;
    };

    struct GetComponentToWorldParams
    {
        FTransform ReturnValue;
    };

    struct GetAnimInstanceParams
    {
        void* ReturnValue;
    };

    struct GetCameraLocationParams
    {
        FVector ReturnValue;
    };

    struct GetCameraRotationParams
    {
        FRotator ReturnValue;
    };

    struct GetFOVAngleParams
    {
        float ReturnValue;
    };

    struct ConvStringToNameParams
    {
        FString InString;
        FNameValue ReturnValue;
    };

    struct ConvNameToStringParams
    {
        FNameValue InName;
        FString ReturnValue;
    };

    struct LineOfSightToParams
    {
        void* Other;
        FVector ViewPoint;
        bool bAlternateChecks;
        bool ReturnValue;
    };

    struct ProjectWorldToScreenParams
    {
        FVector WorldLocation;
        FVector2D ScreenLocation;
        bool bPlayerViewportRelative;
        bool ReturnValue;
    };

    struct GetHealthParams
    {
        float ReturnValue;
    };

    struct GetBoneNameParams
    {
        std::int32_t BoneIndex;
        FNameValue ReturnValue;
    };

    struct GetSocketLocationParams
    {
        FNameValue InSocketName;
        FVector ReturnValue;
    };

    struct GetSocketRotationParams
    {
        FNameValue InSocketName;
        FRotator ReturnValue;
    };

    struct GetAllSocketNamesParams
    {
        TArray<FNameValue> ReturnValue;
    };

    struct GetFactionParams
    {
        void* ReturnValue;
    };

    struct GetVitalityComponentParams
    {
        void* ReturnValue;
    };

    struct GetWeaponBehaviorComponentParams
    {
        void* ReturnValue;
    };

    struct GetWeaponStatsParams
    {
        engine_funcs::WeaponStatsBuffer ReturnValue;
    };

    struct GetWeaponComponentParams
    {
        void* ReturnValue;
    };

    struct GetProjectileDataParams
    {
        void* WeaponComponent;
        FNameValue AmmoModifier;
        void* ReturnValue;
    };

    static_assert(offsetof(GetProjectileDataParams, AmmoModifier) == 0x8);
    static_assert(offsetof(GetProjectileDataParams, ReturnValue) == 0x10);
    static_assert(sizeof(GetProjectileDataParams) == 0x18);

    struct GetProjectileStatsParams
    {
        FNameValue AmmoModifier;
        engine_funcs::ProjectileStatsBuffer ReturnValue;
    };

    static_assert(offsetof(GetProjectileStatsParams, ReturnValue) == 0x8);
    static_assert(sizeof(GetProjectileStatsParams) == 0x320);

    // SDK layout for /Script/WDGame.WDArtilleryRangeLibrary.SimulateImpactDistance.
    // Keep these payloads opaque here: engine_funcs only marshals the two reflected
    // structs into ProcessEvent and does not interpret their obfuscated fields.
    struct SimulateImpactDistanceParams
    {
        engine_funcs::ProjectileStatsBuffer ProjectileStats;
        engine_funcs::WeaponStatsBuffer WeaponStats;
        float SightUnits;
        float InitialHeightMeters;
        float AirDensity;
        float ReturnValue;
    };

    static_assert(offsetof(SimulateImpactDistanceParams, WeaponStats) == 0x318);
    static_assert(offsetof(SimulateImpactDistanceParams, SightUnits) == 0x1160);
    static_assert(offsetof(SimulateImpactDistanceParams, InitialHeightMeters) == 0x1164);
    static_assert(offsetof(SimulateImpactDistanceParams, AirDensity) == 0x1168);
    static_assert(offsetof(SimulateImpactDistanceParams, ReturnValue) == 0x116C);
    static_assert(sizeof(SimulateImpactDistanceParams) == 0x1170);

    struct SetControlRotationParams
    {
        FRotator NewRotation;
    };

    struct GetControlRotationParams
    {
        FRotator ReturnValue;
    };

    struct RInterpToParams
    {
        FRotator Current;
        FRotator Target;
        float DeltaTime;
        float InterpSpeed;
        FRotator ReturnValue;
    };

    struct FindLookAtRotationParams
    {
        FVector Start;
        FVector Target;
        FRotator ReturnValue;
    };

    struct K2SetActorRotationParams
    {
        FRotator NewRotation;
        bool bTeleportPhysics;
        bool ReturnValue;
    };

    struct FloatInputParams
    {
        float Val;
    };

    // Enhanced Input stores an LWC FVector followed by the value-type enum.
    // Boolean actions use X=1 and type 0; the reflected parameter is 0x20 bytes.
    struct InputActionValueRaw
    {
        FVector Value;
        std::uint8_t ValueType;
        std::uint8_t Padding[7];
    };

    static_assert(sizeof(InputActionValueRaw) == 0x20);

    struct InputFireFlaresParams
    {
        InputActionValueRaw Value;
    };

    static_assert(sizeof(InputFireFlaresParams) == 0x20);

} // namespace

bool engine_funcs::init()
{
    if (g_functions_ready && g_fn_get_weapon_stats && g_fn_get_weapon_component && g_fn_get_projectile_data && g_fn_get_projectile_stats && g_default_wd_artillery_range_library && g_fn_simulate_impact_distance && g_default_subsystem_blueprint_library && g_fn_get_world_subsystem && g_fn_get_air_density && g_uclass_wd_weather_subsystem && g_uclass_wd_rotary_vehicle && g_fn_input_fire_flares)
        return g_functions_ready;

    auto resolve_once = [](void*& slot, const wchar_t* name)
    {
        if (!slot)
            slot = engine::static_find_object(nullptr, nullptr, name);
    };
    auto resolve_function = [&](void*& slot, const wchar_t* colon_name, const wchar_t* dot_name)
    {
        resolve_once(slot, colon_name);
        resolve_once(slot, dot_name);
    };
    const ULONGLONG now = frame_ticks;
    if (g_last_resolve_attempt && now - g_last_resolve_attempt < 1000)
        return g_functions_ready;
    g_last_resolve_attempt = now;

    resolve_once(g_default_gameplay_statics, L"/Script/Engine.Default__GameplayStatics");
    resolve_once(g_default_subsystem_blueprint_library, L"/Script/Engine.Default__SubsystemBlueprintLibrary");
    resolve_function(g_fn_get_world_subsystem, L"/Script/Engine.SubsystemBlueprintLibrary:GetWorldSubsystem", L"/Script/Engine.SubsystemBlueprintLibrary.GetWorldSubsystem");
    resolve_function(g_fn_get_air_density, L"/Script/WDGame.WDWeatherSubsystem:GetAirDensity", L"/Script/WDGame.WDWeatherSubsystem.GetAirDensity");
    resolve_once(g_uclass_wd_weather_subsystem, L"/Script/WDGame.WDWeatherSubsystem");

    resolve_once(g_fn_get_player_controller, L"/Script/Engine.GameplayStatics:GetPlayerController");

    resolve_once(g_fn_get_player_camera_manager, L"/Script/Engine.GameplayStatics:GetPlayerCameraManager");

    resolve_once(g_fn_get_player_state, L"/Script/Engine.GameplayStatics:GetPlayerState");

    resolve_function(g_fn_get_world_delta_seconds, L"/Script/Engine.GameplayStatics:GetWorldDeltaSeconds", L"/Script/Engine.GameplayStatics.GetWorldDeltaSeconds");

    resolve_function(g_fn_get_time_seconds, L"/Script/Engine.GameplayStatics:GetTimeSeconds", L"/Script/Engine.GameplayStatics.GetTimeSeconds");

    resolve_once(g_fn_get_camera_location, L"/Script/Engine.PlayerCameraManager:GetCameraLocation");

    resolve_once(g_fn_get_camera_rotation, L"/Script/Engine.PlayerCameraManager:GetCameraRotation");

    resolve_once(g_fn_get_fov_angle, L"/Script/Engine.PlayerCameraManager:GetFOVAngle");

    resolve_once(g_fn_k2_get_pawn, L"/Script/Engine.Controller:K2_GetPawn");

    resolve_once(g_fn_k2_get_root_component, L"/Script/Engine.Actor:K2_GetRootComponent");

    resolve_once(g_fn_get_velocity, L"/Script/Engine.Actor:GetVelocity");

    resolve_once(g_fn_k2_get_component_to_world, L"/Script/Engine.SceneComponent:K2_GetComponentToWorld");

    resolve_once(g_fn_get_anim_instance, L"/Script/Engine.SkeletalMeshComponent:GetAnimInstance");

    resolve_once(g_default_kismet_string_library, L"/Script/Engine.Default__KismetStringLibrary");

    resolve_once(g_fn_conv_string_to_name, L"/Script/Engine.KismetStringLibrary:Conv_StringToName");

    resolve_once(g_fn_conv_name_to_string, L"/Script/Engine.KismetStringLibrary:Conv_NameToString");

    resolve_once(g_fn_line_of_sight, L"/Script/Engine.Controller:LineOfSightTo");

    resolve_once(g_fn_project_world, L"/Script/Engine.PlayerController:ProjectWorldLocationToScreen");

    resolve_once(g_fn_get_current_health, L"/Script/WDGame.WDVitalityComponent:GetCurrentHealth");

    resolve_once(g_fn_get_max_health, L"/Script/WDGame.WDVitalityComponent:GetMaxHealth");

    resolve_once(g_fn_get_bone_name, L"/Script/Engine.SkinnedMeshComponent:GetBoneName");

    resolve_once(g_fn_get_socket_location, L"/Script/Engine.SceneComponent:GetSocketLocation");
    resolve_once(g_fn_get_socket_rotation, L"/Script/Engine.SceneComponent:GetSocketRotation");

    resolve_once(g_fn_get_all_socket_names, L"/Script/Engine.SceneComponent:GetAllSocketNames");

    resolve_once(g_fn_get_faction, L"/Script/WDGame.WDPlayerStateSession:GetFaction");

    resolve_once(g_fn_get_vitality_component, L"/Script/WDGame.WDMoverCharacter:GetVitalityComponent");

    resolve_once(g_fn_get_weapon_behavior_component, L"/Script/WDGame.WDCharacterAnimInstance:GetWeaponBehaviorComponent");
    resolve_function(g_fn_get_weapon_stats, L"/Script/WDGame.WDWeaponComponent:GetWeaponStats", L"/Script/WDGame.WDWeaponComponent.GetWeaponStats");
    resolve_function(g_fn_get_weapon_component, L"/Script/WDGame.WDVehicleWeaponExtension:GetWeaponComponent", L"/Script/WDGame.WDVehicleWeaponExtension.GetWeaponComponent");
    resolve_function(g_fn_get_projectile_data, L"/Script/WDGame.WDWeaponOwner:GetProjectileData", L"/Script/WDGame.WDWeaponOwner.GetProjectileData");
    resolve_function(g_fn_get_projectile_stats, L"/Script/WDGame.WDProjectileData:BP_GetProjectileStats", L"/Script/WDGame.WDProjectileData.BP_GetProjectileStats");

    resolve_once(g_default_wd_artillery_range_library, L"/Script/WDGame.Default__WDArtilleryRangeLibrary");
    resolve_function(g_fn_simulate_impact_distance, L"/Script/WDGame.WDArtilleryRangeLibrary:SimulateImpactDistance", L"/Script/WDGame.WDArtilleryRangeLibrary.SimulateImpactDistance");

    resolve_once(g_fn_set_control_rotation, L"/Script/Engine.Controller:SetControlRotation");

    resolve_once(g_fn_get_control_rotation, L"/Script/Engine.Controller:GetControlRotation");

    resolve_once(g_default_kismet_math_library, L"/Script/Engine.Default__KismetMathLibrary");

    resolve_once(g_fn_rinterp_to, L"/Script/Engine.KismetMathLibrary:RInterpTo");

    resolve_once(g_fn_find_look_at_rotation, L"/Script/Engine.KismetMathLibrary:FindLookAtRotation");

    resolve_once(g_fn_k2_set_actor_rotation, L"/Script/Engine.Actor:K2_SetActorRotation");

    resolve_once(g_fn_add_yaw_input, L"/Script/Engine.PlayerController:AddYawInput");
    resolve_once(g_fn_add_pitch_input, L"/Script/Engine.PlayerController:AddPitchInput");

    resolve_function(g_fn_object_is_a, L"/Script/Engine.GameplayStatics:ObjectIsA", L"/Script/Engine.GameplayStatics.ObjectIsA");
    resolve_once(g_uclass_modular_vehicle, L"/Script/ModularVehicles.ModularVehicle");
    resolve_once(g_uclass_wd_rotary_vehicle, L"/Script/WDGame.WDRotaryVehicle");
    resolve_once(g_uclass_wd_airplane_vehicle, L"/Script/WDGame.WDAirplaneVehicle");
    resolve_function(g_fn_input_fire_flares, L"/Script/WDGame.WDRotaryVehicle:InputFireFlares", L"/Script/WDGame.WDRotaryVehicle.InputFireFlares");

    g_functions_ready = g_default_gameplay_statics && g_fn_get_player_controller &&
                        g_fn_get_player_camera_manager && g_fn_get_player_state &&
                        g_fn_get_world_delta_seconds && g_fn_get_time_seconds &&
                        g_fn_get_camera_location && g_fn_get_camera_rotation &&
                        g_fn_get_fov_angle && g_fn_k2_get_pawn && g_fn_k2_get_root_component &&
                        g_fn_k2_get_component_to_world && g_fn_get_anim_instance &&
                        g_default_kismet_string_library &&
                        g_fn_conv_string_to_name && g_fn_conv_name_to_string &&
                        g_fn_line_of_sight && g_fn_project_world &&
                        g_fn_get_current_health && g_fn_get_max_health &&
                        g_fn_get_bone_name && g_fn_get_socket_location &&
                        g_fn_get_faction && g_fn_get_vitality_component &&
                        g_fn_get_weapon_behavior_component &&
                        g_fn_set_control_rotation && g_fn_get_control_rotation &&
                        g_default_kismet_math_library && g_fn_rinterp_to && g_fn_find_look_at_rotation;

    return g_functions_ready;
}

void engine_funcs::shutdown()
{
    g_functions_ready = false;
    g_last_resolve_attempt = 0;
    g_default_gameplay_statics = nullptr;
    g_default_subsystem_blueprint_library = nullptr;
    g_fn_get_player_controller = nullptr;
    g_fn_get_player_camera_manager = nullptr;
    g_fn_get_player_state = nullptr;
    g_fn_get_world_delta_seconds = nullptr;
    g_fn_get_time_seconds = nullptr;
    g_fn_get_world_subsystem = nullptr;
    g_fn_get_air_density = nullptr;
    g_fn_get_camera_location = nullptr;
    g_fn_get_camera_rotation = nullptr;
    g_fn_get_fov_angle = nullptr;
    g_fn_k2_get_pawn = nullptr;
    g_fn_k2_get_root_component = nullptr;
    g_fn_get_velocity = nullptr;
    g_fn_k2_get_component_to_world = nullptr;
    g_fn_get_anim_instance = nullptr;
    g_default_kismet_string_library = nullptr;
    g_fn_conv_string_to_name = nullptr;
    g_fn_conv_name_to_string = nullptr;
    g_fn_line_of_sight = nullptr;
    g_fn_project_world = nullptr;
    g_fn_get_current_health = nullptr;
    g_fn_get_max_health = nullptr;
    g_fn_get_bone_name = nullptr;
    g_fn_get_socket_location = nullptr;
    g_fn_get_socket_rotation = nullptr;
    g_fn_get_all_socket_names = nullptr;
    g_fn_get_faction = nullptr;
    g_fn_get_vitality_component = nullptr;
    g_fn_get_weapon_behavior_component = nullptr;
    g_fn_get_weapon_stats = nullptr;
    g_fn_get_weapon_component = nullptr;
    g_fn_get_projectile_data = nullptr;
    g_fn_get_projectile_stats = nullptr;
    g_default_wd_artillery_range_library = nullptr;
    g_fn_simulate_impact_distance = nullptr;
    g_fn_set_control_rotation = nullptr;
    g_fn_get_control_rotation = nullptr;
    g_default_kismet_math_library = nullptr;
    g_fn_rinterp_to = nullptr;
    g_fn_find_look_at_rotation = nullptr;
    g_fn_k2_set_actor_rotation = nullptr;
    g_fn_add_yaw_input = nullptr;
    g_fn_add_pitch_input = nullptr;
    g_fn_object_is_a = nullptr;
    g_uclass_modular_vehicle = nullptr;
    g_uclass_wd_rotary_vehicle = nullptr;
    g_uclass_wd_airplane_vehicle = nullptr;
    g_uclass_wd_weather_subsystem = nullptr;
    g_fn_input_fire_flares = nullptr;
}

bool engine_funcs::bone_functions_ready()
{
    return g_fn_get_bone_name != nullptr && g_fn_get_socket_location != nullptr;
}

void* engine_funcs::get_player_controller(void* world_context, std::int32_t player_index)
{
    if (!g_default_gameplay_statics || !g_fn_get_player_controller || !world_context || player_index < 0)
        return nullptr;

    GetLocalPlayerObjectParams params{};
    params.WorldContextObject = world_context;
    params.PlayerIndex = player_index;
    if (!engine::call_process_event(g_default_gameplay_statics, g_fn_get_player_controller, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

void* engine_funcs::get_player_camera_manager(void* world_context, std::int32_t player_index)
{
    if (!g_default_gameplay_statics || !g_fn_get_player_camera_manager || !world_context || player_index < 0)
        return nullptr;

    GetLocalPlayerObjectParams params{};
    params.WorldContextObject = world_context;
    params.PlayerIndex = player_index;
    if (!engine::call_process_event(g_default_gameplay_statics, g_fn_get_player_camera_manager, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

void* engine_funcs::get_player_state(void* world_context, std::int32_t player_index)
{
    if (!g_default_gameplay_statics || !g_fn_get_player_state || !world_context || player_index < 0)
        return nullptr;

    GetLocalPlayerObjectParams params{};
    params.WorldContextObject = world_context;
    params.PlayerIndex = player_index;
    if (!engine::call_process_event(g_default_gameplay_statics, g_fn_get_player_state, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

bool engine_funcs::get_world_delta_seconds(void* world_context, double& delta_seconds_out)
{
    delta_seconds_out = 0.0;
    if (!g_default_gameplay_statics || !g_fn_get_world_delta_seconds || !world_context)
        return false;

    WorldDoubleParams params{};
    params.WorldContextObject = world_context;
    if (!engine::call_process_event(g_default_gameplay_statics, g_fn_get_world_delta_seconds, &params) || !std::isfinite(params.ReturnValue) || params.ReturnValue <= 0.0 || params.ReturnValue > 1.0)
        return false;
    delta_seconds_out = params.ReturnValue;
    return true;
}

bool engine_funcs::get_time_seconds(void* world_context, double& time_seconds_out)
{
    time_seconds_out = 0.0;
    if (!g_default_gameplay_statics || !g_fn_get_time_seconds || !world_context)
        return false;

    WorldDoubleParams params{};
    params.WorldContextObject = world_context;
    if (!engine::call_process_event(g_default_gameplay_statics, g_fn_get_time_seconds, &params) || !std::isfinite(params.ReturnValue) || params.ReturnValue < 0.0)
        return false;
    time_seconds_out = params.ReturnValue;
    return true;
}

bool engine_funcs::get_air_density(void* world_context, float& air_density_out)
{
    air_density_out = 0.f;
    if (!g_default_subsystem_blueprint_library || !g_fn_get_world_subsystem || !g_fn_get_air_density || !g_uclass_wd_weather_subsystem || !world_context)
        return false;

    void* subsystem = engine_funcs::get_world_subsystem(world_context, g_uclass_wd_weather_subsystem);
    if (!subsystem)
        return false;

    GetAirDensityParams density_params{};
    if (!engine::call_process_event(subsystem, g_fn_get_air_density, &density_params) || !std::isfinite(density_params.ReturnValue) || density_params.ReturnValue <= 0.05f || density_params.ReturnValue > 5.0f)
        return false;

    air_density_out = density_params.ReturnValue;
    return true;
}

void* engine_funcs::get_world_subsystem(void* world_context, void* subsystem_class)
{
    if (!g_default_subsystem_blueprint_library || !g_fn_get_world_subsystem || !world_context || !subsystem_class)
        return nullptr;

    GetWorldSubsystemParams params{};
    params.WorldContextObject = world_context;
    params.Class = subsystem_class;
    if (!engine::call_process_event(g_default_subsystem_blueprint_library, g_fn_get_world_subsystem, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

bool engine_funcs::get_camera_location(void* camera_manager, FVector& location_out)
{
    if (!g_fn_get_camera_location || !camera_manager)
        return false;

    GetCameraLocationParams params{};
    if (!engine::call_process_event(camera_manager, g_fn_get_camera_location, &params) || !std::isfinite(params.ReturnValue.X) || !std::isfinite(params.ReturnValue.Y) || !std::isfinite(params.ReturnValue.Z))
        return false;
    location_out = params.ReturnValue;
    return true;
}

bool engine_funcs::get_camera_rotation(void* camera_manager, FRotator& rotation_out)
{
    if (!g_fn_get_camera_rotation || !camera_manager)
        return false;

    GetCameraRotationParams params{};
    if (!engine::call_process_event(camera_manager, g_fn_get_camera_rotation, &params) || !std::isfinite(params.ReturnValue.Pitch) || !std::isfinite(params.ReturnValue.Yaw) || !std::isfinite(params.ReturnValue.Roll))
        return false;
    rotation_out = params.ReturnValue;
    return true;
}

bool engine_funcs::get_fov_angle(void* camera_manager, float& fov_out)
{
    if (!g_fn_get_fov_angle || !camera_manager)
        return false;

    GetFOVAngleParams params{};
    if (!engine::call_process_event(camera_manager, g_fn_get_fov_angle, &params) || !std::isfinite(params.ReturnValue) || params.ReturnValue <= 1.f || params.ReturnValue >= 179.f)
        return false;
    fov_out = params.ReturnValue;
    return true;
}

void* engine_funcs::k2_get_pawn(void* controller)
{
    if (!g_fn_k2_get_pawn || !controller)
        return nullptr;

    GetPawnParams params{};
    if (!engine::call_process_event(controller, g_fn_k2_get_pawn, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

void* engine_funcs::k2_get_root_component(void* actor)
{
    if (!g_fn_k2_get_root_component || !actor)
        return nullptr;

    GetRootComponentParams params{};
    if (!engine::call_process_event(actor, g_fn_k2_get_root_component, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

bool engine_funcs::get_velocity(void* actor, FVector& velocity_out)
{
    if (!g_fn_get_velocity || !actor)
        return false;

    GetVelocityParams params{};
    if (!engine::call_process_event(actor, g_fn_get_velocity, &params) || !std::isfinite(params.ReturnValue.X) || !std::isfinite(params.ReturnValue.Y) || !std::isfinite(params.ReturnValue.Z))
        return false;

    velocity_out = params.ReturnValue;
    return true;
}

bool engine_funcs::k2_get_component_to_world(void* scene_component, FTransform& transform_out)
{
    if (!g_fn_k2_get_component_to_world || !scene_component)
        return false;

    GetComponentToWorldParams params{};
    if (!engine::call_process_event(scene_component, g_fn_k2_get_component_to_world, &params))
        return false;

    const FTransform& value = params.ReturnValue;
    if (!std::isfinite(value.Rotation.X) || !std::isfinite(value.Rotation.Y) || !std::isfinite(value.Rotation.Z) || !std::isfinite(value.Rotation.W) || !std::isfinite(value.Translation.X) || !std::isfinite(value.Translation.Y) || !std::isfinite(value.Translation.Z) || !std::isfinite(value.Scale3D.X) || !std::isfinite(value.Scale3D.Y) || !std::isfinite(value.Scale3D.Z))
        return false;

    transform_out = value;
    return true;
}

void* engine_funcs::get_anim_instance(void* skeletal_mesh_component)
{
    if (!g_fn_get_anim_instance || !skeletal_mesh_component)
        return nullptr;

    GetAnimInstanceParams params{};
    if (!engine::call_process_event(skeletal_mesh_component, g_fn_get_anim_instance, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

FNameValue engine_funcs::conv_string_to_name(const FString& value)
{
    if (!g_default_kismet_string_library || !g_fn_conv_string_to_name || !value.IsValid())
        return {};

    ConvStringToNameParams params{};
    params.InString = value;
    if (!engine::call_process_event(g_default_kismet_string_library, g_fn_conv_string_to_name, &params))
        return {};
    return params.ReturnValue;
}

FString engine_funcs::conv_name_to_string(FNameValue value)
{
    // Readable object memory can still contain a stale or unrelated name ID.
    // The native conversion dereferences the name pool without validating it.
    if (!g_default_kismet_string_library || !g_fn_conv_name_to_string || !FName::IsValid(value.ComparisonIndex))
        return {};

    ConvNameToStringParams params{};
    params.InName = value;
    if (!engine::call_process_event(g_default_kismet_string_library, g_fn_conv_name_to_string, &params) || !params.ReturnValue.IsValid())
        return {};
    return params.ReturnValue;
}

#ifdef WD_TEST
void engine_funcs::test_name_conversion(void* library, void* function)
{
    g_default_kismet_string_library = library;
    g_fn_conv_name_to_string = function;
}
#endif

void engine_funcs::release_string(FString& value)
{
    void* allocation = value.data();
    if (allocation && offsets::Functions::FreeObjectName)
    {
        const std::uintptr_t free_address = offsets::base + offsets::Functions::FreeObjectName;
        if (is_valid_ptr(reinterpret_cast<const void*>(free_address)))
            reinterpret_cast<engine::FreeObjectNameFn>(free_address)(reinterpret_cast<std::uintptr_t>(allocation));
    }
}

bool engine_funcs::line_of_sight_to(void* controller, void* target_actor)
{
    if (!g_fn_line_of_sight || !controller || !target_actor)
        return false;

    LineOfSightToParams params{};
    params.Other = target_actor;
    params.bAlternateChecks = false;
    params.ReturnValue = false;
    if (!engine::call_process_event(controller, g_fn_line_of_sight, &params))
        return false;
    return params.ReturnValue;
}

bool engine_funcs::project_world_to_screen(void* controller, const FVector& world_location, FVector2D& screen_out)
{
    if (!g_fn_project_world || !controller)
        return false;

    ProjectWorldToScreenParams params{};
    params.WorldLocation = world_location;
    params.bPlayerViewportRelative = false;
    params.ReturnValue = false;

    if (!std::isfinite(world_location.X) || !std::isfinite(world_location.Y) || !std::isfinite(world_location.Z) || !engine::call_process_event(controller, g_fn_project_world, &params))
        return false;

    if (params.ReturnValue && std::isfinite(params.ScreenLocation.X) && std::isfinite(params.ScreenLocation.Y))
        screen_out = params.ScreenLocation;
    else
        params.ReturnValue = false;

    return params.ReturnValue;
}

float engine_funcs::get_current_health(void* vitality_component)
{
    if (!g_fn_get_current_health || !vitality_component)
        return 0.f;

    GetHealthParams params{};
    if (!engine::call_process_event(vitality_component, g_fn_get_current_health, &params) || !std::isfinite(params.ReturnValue))
        return 0.f;
    return params.ReturnValue;
}

float engine_funcs::get_max_health(void* vitality_component)
{
    if (!g_fn_get_max_health || !vitality_component)
        return 0.f;

    GetHealthParams params{};
    if (!engine::call_process_event(vitality_component, g_fn_get_max_health, &params) || !std::isfinite(params.ReturnValue))
        return 0.f;
    return params.ReturnValue;
}

FNameValue engine_funcs::get_bone_name(void* mesh, std::int32_t bone_index)
{
    if (!g_fn_get_bone_name || !mesh)
        return {};

    GetBoneNameParams params{};
    params.BoneIndex = bone_index;
    if (!engine::call_process_event(mesh, g_fn_get_bone_name, &params))
        return {};
    return params.ReturnValue;
}

FVector engine_funcs::get_socket_location(void* mesh, FNameValue socket_name)
{
    if (!g_fn_get_socket_location || !mesh)
        return {};

    GetSocketLocationParams params{};
    params.InSocketName = socket_name;
    if (!engine::call_process_event(mesh, g_fn_get_socket_location, &params) || !std::isfinite(params.ReturnValue.X) || !std::isfinite(params.ReturnValue.Y) || !std::isfinite(params.ReturnValue.Z))
        return {};
    return params.ReturnValue;
}

FRotator engine_funcs::get_socket_rotation(void* mesh, FNameValue socket_name)
{
    if (!g_fn_get_socket_rotation || !mesh)
        return {};
    GetSocketRotationParams params{};
    params.InSocketName = socket_name;
    if (!engine::call_process_event(mesh, g_fn_get_socket_rotation, &params) || !std::isfinite(params.ReturnValue.Pitch) || !std::isfinite(params.ReturnValue.Yaw) || !std::isfinite(params.ReturnValue.Roll))
        return {};
    return params.ReturnValue;
}

TArray<FNameValue> engine_funcs::get_all_socket_names(void* scene_component)
{
    if (!g_fn_get_all_socket_names || !scene_component)
        return {};
    GetAllSocketNamesParams params{};
    if (!engine::call_process_event(scene_component, g_fn_get_all_socket_names, &params))
        return {};
    return params.ReturnValue;
}

void* engine_funcs::get_faction(void* player_state)
{
    if (!g_fn_get_faction || !player_state)
        return nullptr;

    GetFactionParams params{};
    if (!engine::call_process_event(player_state, g_fn_get_faction, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

void* engine_funcs::get_vitality_component(void* character)
{
    if (!g_fn_get_vitality_component || !character)
        return nullptr;

    GetVitalityComponentParams params{};
    if (!engine::call_process_event(character, g_fn_get_vitality_component, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

void* engine_funcs::get_weapon_behavior_component(void* character_anim_instance)
{
    if (!g_fn_get_weapon_behavior_component || !character_anim_instance)
        return nullptr;

    GetWeaponBehaviorComponentParams params{};
    if (!engine::call_process_event(character_anim_instance, g_fn_get_weapon_behavior_component, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

bool engine_funcs::get_weapon_stats(void* weapon_component, WeaponStatsBuffer& weapon_stats_out)
{
    weapon_stats_out = {};
    if (!g_fn_get_weapon_stats || !weapon_component)
        return false;

    GetWeaponStatsParams params{};
    if (!engine::call_process_event(weapon_component, g_fn_get_weapon_stats, &params))
        return false;

    weapon_stats_out = params.ReturnValue;
    return true;
}

void* engine_funcs::get_weapon_component(void* vehicle_weapon_extension)
{
    if (!g_fn_get_weapon_component || !vehicle_weapon_extension)
        return nullptr;

    GetWeaponComponentParams params{};
    if (!engine::call_process_event(vehicle_weapon_extension, g_fn_get_weapon_component, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;
    return params.ReturnValue;
}

void* engine_funcs::get_projectile_data(void* weapon_owner, void* weapon_component, FNameValue& ammo_modifier_out)
{
    ammo_modifier_out = {};
    if (!g_fn_get_projectile_data || !weapon_owner || !weapon_component)
        return nullptr;

    GetProjectileDataParams params{};
    params.WeaponComponent = weapon_component;
    if (!engine::call_process_event(weapon_owner, g_fn_get_projectile_data, &params) || !is_valid_ptr(params.ReturnValue))
        return nullptr;

    ammo_modifier_out = params.AmmoModifier;
    return params.ReturnValue;
}

bool engine_funcs::get_projectile_stats(void* projectile_data, FNameValue ammo_modifier, ProjectileStatsBuffer& projectile_stats_out)
{
    projectile_stats_out = {};
    if (!g_fn_get_projectile_stats || !projectile_data)
        return false;

    GetProjectileStatsParams params{};
    params.AmmoModifier = ammo_modifier;
    if (!engine::call_process_event(projectile_data, g_fn_get_projectile_stats, &params))
        return false;

    projectile_stats_out = params.ReturnValue;
    return true;
}

bool engine_funcs::get_ballistic_inputs(void* vehicle_weapon_extension, ProjectileStatsBuffer& projectile_stats_out, WeaponStatsBuffer& weapon_stats_out, FNameValue& ammo_modifier_out, void*& projectile_data_out)
{
    projectile_stats_out = {};
    weapon_stats_out = {};
    ammo_modifier_out = {};
    projectile_data_out = nullptr;

    void* weapon_component = get_weapon_component(vehicle_weapon_extension);
    if (!weapon_component)
        return false;

    projectile_data_out = get_projectile_data(vehicle_weapon_extension, weapon_component, ammo_modifier_out);
    if (!projectile_data_out)
        return false;

    if (!get_projectile_stats(projectile_data_out, ammo_modifier_out, projectile_stats_out) || !get_weapon_stats(weapon_component, weapon_stats_out))
    {
        projectile_stats_out = {};
        weapon_stats_out = {};
        projectile_data_out = nullptr;
        return false;
    }

    return true;
}

bool engine_funcs::simulate_impact_distance(const ProjectileStatsBuffer& projectile_stats, const WeaponStatsBuffer& weapon_stats, float sight_units, float initial_height_m, float air_density, float& impact_distance_out)
{
    impact_distance_out = 0.0f;
    if (!g_default_wd_artillery_range_library || !g_fn_simulate_impact_distance || !std::isfinite(sight_units) || !std::isfinite(initial_height_m) || !std::isfinite(air_density))
        return false;

    SimulateImpactDistanceParams params{};
    params.ProjectileStats = projectile_stats;
    params.WeaponStats = weapon_stats;
    params.SightUnits = sight_units;
    params.InitialHeightMeters = initial_height_m;
    params.AirDensity = air_density;
    if (!engine::call_process_event(g_default_wd_artillery_range_library, g_fn_simulate_impact_distance, &params) || !std::isfinite(params.ReturnValue))
        return false;

    impact_distance_out = params.ReturnValue;
    return true;
}

void engine_funcs::set_control_rotation(void* controller, const FRotator& rotation)
{
    if (!g_fn_set_control_rotation || !controller || !std::isfinite(rotation.Pitch) || !std::isfinite(rotation.Yaw) || !std::isfinite(rotation.Roll))
        return;

    SetControlRotationParams params{};
    params.NewRotation = rotation;
    engine::call_process_event(controller, g_fn_set_control_rotation, &params);
}

FRotator engine_funcs::get_control_rotation(void* controller)
{
    if (!g_fn_get_control_rotation || !controller)
        return {};

    GetControlRotationParams params{};
    if (!engine::call_process_event(controller, g_fn_get_control_rotation, &params) || !std::isfinite(params.ReturnValue.Pitch) || !std::isfinite(params.ReturnValue.Yaw) || !std::isfinite(params.ReturnValue.Roll))
        return {};
    return params.ReturnValue;
}

FRotator engine_funcs::rinterp_to(const FRotator& current, const FRotator& target, float delta_time, float interp_speed)
{
    if (!g_default_kismet_math_library || !g_fn_rinterp_to || !std::isfinite(delta_time) || !std::isfinite(interp_speed))
        return current;

    RInterpToParams params{};
    params.Current = current;
    params.Target = target;
    params.DeltaTime = delta_time;
    params.InterpSpeed = interp_speed;
    if (!engine::call_process_event(g_default_kismet_math_library, g_fn_rinterp_to, &params) || !std::isfinite(params.ReturnValue.Pitch) || !std::isfinite(params.ReturnValue.Yaw) || !std::isfinite(params.ReturnValue.Roll))
        return current;
    return params.ReturnValue;
}

FRotator engine_funcs::find_look_at_rotation(const FVector& start, const FVector& target)
{
    if (!g_default_kismet_math_library || !g_fn_find_look_at_rotation || !std::isfinite(start.X) || !std::isfinite(start.Y) || !std::isfinite(start.Z) || !std::isfinite(target.X) || !std::isfinite(target.Y) || !std::isfinite(target.Z))
        return {};

    FindLookAtRotationParams params{};
    params.Start = start;
    params.Target = target;
    if (!engine::call_process_event(g_default_kismet_math_library, g_fn_find_look_at_rotation, &params) || !std::isfinite(params.ReturnValue.Pitch) || !std::isfinite(params.ReturnValue.Yaw) || !std::isfinite(params.ReturnValue.Roll))
        return {};
    return params.ReturnValue;
}

bool engine_funcs::k2_set_actor_rotation(void* actor, const FRotator& rotation, bool teleport)
{
    if (!g_fn_k2_set_actor_rotation || !actor || !std::isfinite(rotation.Pitch) || !std::isfinite(rotation.Yaw) || !std::isfinite(rotation.Roll))
        return false;

    K2SetActorRotationParams params{};
    params.NewRotation = rotation;
    params.bTeleportPhysics = teleport;
    params.ReturnValue = false;
    if (!engine::call_process_event(actor, g_fn_k2_set_actor_rotation, &params))
        return false;
    return params.ReturnValue;
}

void engine_funcs::add_yaw_input(void* controller, float val)
{
    if (!g_fn_add_yaw_input || !controller || !std::isfinite(val))
        return;
    FloatInputParams params{};
    params.Val = val;
    engine::call_process_event(controller, g_fn_add_yaw_input, &params);
}

void engine_funcs::add_pitch_input(void* controller, float val)
{
    if (!g_fn_add_pitch_input || !controller || !std::isfinite(val))
        return;
    FloatInputParams params{};
    params.Val = val;
    engine::call_process_event(controller, g_fn_add_pitch_input, &params);
}

bool engine_funcs::object_is_a(void* object, void* test_class)
{
    if (!g_default_gameplay_statics || !g_fn_object_is_a || !object || !test_class)
        return false;
    struct
    {
        void* Object;
        void* TestClass;
        bool ReturnValue;
    } params{};
    params.Object = object;
    params.TestClass = test_class;
    if (!engine::call_process_event(g_default_gameplay_statics, g_fn_object_is_a, &params))
        return false;
    return params.ReturnValue;
}

void* engine_funcs::get_vehicle_base_class()
{
    return g_uclass_modular_vehicle;
}

bool engine_funcs::is_rotary_vehicle(void* vehicle)
{
    if (!vehicle || !g_uclass_wd_rotary_vehicle)
        return false;

    // The Blueprint ObjectIsA call is not available during every world
    // transition.  Walk the native UClass super chain first so the occupied
    // helicopter gate cannot remain false merely because that helper has not
    // been resolved yet.
    std::uintptr_t cls = read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(vehicle) + offsets::UObject::ClassPrivate);
    for (int depth = 0; cls && depth < 64; ++depth)
    {
        if (reinterpret_cast<void*>(cls) == g_uclass_wd_rotary_vehicle)
            return true;
        if (!is_valid_ptr(reinterpret_cast<void*>(cls)))
            break;
        cls = read<std::uintptr_t>(cls + offsets::UStruct::SuperStruct);
    }

    return object_is_a(vehicle, g_uclass_wd_rotary_vehicle);
}

bool engine_funcs::input_fire_flares(void* rotary_vehicle)
{
    if (!rotary_vehicle || !g_fn_input_fire_flares || !is_rotary_vehicle(rotary_vehicle))
        return false;

    InputFireFlaresParams params{};
    params.Value.Value = FVector(1.0, 0.0, 0.0);
    params.Value.ValueType = 0; // EInputActionValueType::Boolean
    return engine::call_process_event(rotary_vehicle, g_fn_input_fire_flares, &params);
}
