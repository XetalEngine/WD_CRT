#pragma once
#include "structs.h"

namespace engine_funcs
{

    struct ProjectileStatsBuffer
    {
        std::uint8_t Data[0x318];
    };

    struct WeaponStatsBuffer
    {
        std::uint8_t Data[0xE48];
    };

    static_assert(sizeof(ProjectileStatsBuffer) == 0x318);
    static_assert(sizeof(WeaponStatsBuffer) == 0xE48);

    bool init();
    void shutdown();
    bool bone_functions_ready();
#ifdef WD_TEST
    void test_name_conversion(void* library, void* function);
#endif

    void* get_player_controller(void* world_context, std::int32_t player_index = 0);
    void* get_player_camera_manager(void* world_context, std::int32_t player_index = 0);
    void* get_player_state(void* world_context, std::int32_t player_index = 0);
    bool get_world_delta_seconds(void* world_context, double& delta_seconds_out);
    bool get_time_seconds(void* world_context, double& time_seconds_out);
    void* get_world_subsystem(void* world_context, void* subsystem_class);
    bool get_air_density(void* world_context, float& air_density_out);
    bool get_camera_location(void* camera_manager, FVector& location_out);
    bool get_camera_rotation(void* camera_manager, FRotator& rotation_out);
    bool get_fov_angle(void* camera_manager, float& fov_out);
    void* k2_get_pawn(void* controller);
    void* k2_get_root_component(void* actor);
    bool get_velocity(void* actor, FVector& velocity_out);
    bool k2_get_component_to_world(void* scene_component, FTransform& transform_out);
    void* get_anim_instance(void* skeletal_mesh_component);

    FNameValue conv_string_to_name(const FString& value);
    FString conv_name_to_string(FNameValue value);
    void release_string(FString& value);

    bool line_of_sight_to(void* controller, void* target_actor);
    bool project_world_to_screen(void* controller, const FVector& world_location, FVector2D& screen_out);

    float get_current_health(void* vitality_component);
    float get_max_health(void* vitality_component);

    FNameValue get_bone_name(void* mesh, std::int32_t bone_index);
    FVector get_socket_location(void* mesh, FNameValue socket_name);
    FRotator get_socket_rotation(void* mesh, FNameValue socket_name);
    TArray<FNameValue> get_all_socket_names(void* scene_component);
    void* get_faction(void* player_state);
    void* get_vitality_component(void* character);
    void* get_weapon_behavior_component(void* character_anim_instance);
    bool get_weapon_stats(void* weapon_component, WeaponStatsBuffer& weapon_stats_out);
    void* get_weapon_component(void* vehicle_weapon_extension);
    void* get_projectile_data(void* weapon_owner, void* weapon_component, FNameValue& ammo_modifier_out);
    bool get_projectile_stats(void* projectile_data, FNameValue ammo_modifier, ProjectileStatsBuffer& projectile_stats_out);
    bool get_ballistic_inputs(void* vehicle_weapon_extension, ProjectileStatsBuffer& projectile_stats_out, WeaponStatsBuffer& weapon_stats_out, FNameValue& ammo_modifier_out, void*& projectile_data_out);
    bool simulate_impact_distance(const ProjectileStatsBuffer& projectile_stats, const WeaponStatsBuffer& weapon_stats, float sight_units, float initial_height_m, float air_density, float& impact_distance_out);
    void set_control_rotation(void* controller, const FRotator& rotation);
    FRotator get_control_rotation(void* controller);
    FRotator rinterp_to(const FRotator& current, const FRotator& target, float delta_time, float interp_speed);
    FRotator find_look_at_rotation(const FVector& start, const FVector& target);
    bool k2_set_actor_rotation(void* actor, const FRotator& rotation, bool teleport);
    void add_yaw_input(void* controller, float val);
    void add_pitch_input(void* controller, float val);

    bool object_is_a(void* object, void* test_class);
    void* get_vehicle_base_class();
    bool is_rotary_vehicle(void* vehicle);
    // Feeds a digital flare action through the vehicle's native Enhanced Input
    // handler. The caller supplies the currently occupied WDRotaryVehicle.
    bool input_fire_flares(void* rotary_vehicle);

} // namespace engine_funcs
