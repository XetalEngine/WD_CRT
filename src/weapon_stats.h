#pragma once

#include "structs.h"
#include <cstddef>
#include <cstdint>

namespace wdgs::weapon_stats
{

    // Fields ending in _guess are semantic guesses based on the reflected type,
    // neighboring properties and the sample values shown in the runtime dump.
    // The offsets are verified; only those descriptive names are provisional.
    struct FWDWeaponStats
    {
        std::uint8_t opaque_0000[0x90];
        float ballistic_angle_upper_guess; // 0x090, sample 20.0
        float ballistic_angle_lower_guess; // 0x094, sample -40.0
        float gravity_scale_guess;         // 0x098, sample 0.5
        float projectile_lifetime_guess;   // 0x09C, sample 30.0
        bool uses_ballistics_guess;        // 0x0A0, sample true
        std::uint8_t pad_0A1[0x3];
        float TypicalSpeed;          // 0x0A4, reflected name
        bool inherit_velocity_guess; // 0x0A8, sample true
        std::uint8_t opaque_0A9[0x8F];
        float rate_of_fire_rpm_guess; // 0x138, sample 700.0
        std::uint8_t pad_13C[0x4];
        std::uint8_t fire_modes[0x10];        // 0x140, TArray<EWDFireMode>
        std::uint8_t default_fire_mode_guess; // 0x150
        std::uint8_t pad_151[0x3];
        float fire_delay_guess;    // 0x154, sample 0.2
        float recovery_rate_guess; // 0x158, sample 1.2
        bool automatic_fire_guess; // 0x15C, sample true
        std::uint8_t pad_15D[0x3];
        std::int32_t MinZeroingRange; // 0x160, reflected name
        std::int32_t MaxZeroingRange; // 0x164, reflected name
        std::int32_t zeroing_step;    // 0x168, verified 100 m steps
        bool zeroing_enabled_guess;   // 0x16C, sample true
        bool clamp_zeroing_guess;     // 0x16D, sample true
        std::uint8_t opaque_16E[0xA0A];
        float aim_curve_scale_guess; // 0xB78, sample 1.0
        std::uint8_t opaque_B7C[0x8C];
        std::uint8_t fov_shifting_type; // 0xC08, EWDWeaponFOVShiftingType
        std::uint8_t pad_C09[0x3];
        float fov_shift_scale_guess; // 0xC0C, sample 1.5
        std::uint8_t opaque_C10[0x88];
        float fov_blend_time_guess; // 0xC98, sample 0.25
        std::uint8_t opaque_C9C[0x1C];
        float firing_animation_angle_guess; // 0xCB8, sample 45.0
        std::uint8_t opaque_CBC[0x18C];
    };

    static_assert(offsetof(FWDWeaponStats, TypicalSpeed) == 0xA4);
    static_assert(offsetof(FWDWeaponStats, MinZeroingRange) == 0x160);
    static_assert(offsetof(FWDWeaponStats, MaxZeroingRange) == 0x164);
    static_assert(offsetof(FWDWeaponStats, fov_shifting_type) == 0xC08);
    static_assert(sizeof(FWDWeaponStats) == 0xE48);

    struct Snapshot
    {
        std::uintptr_t behavior_component{};
        std::uintptr_t stats_data{};
        std::uintptr_t stats{};

        float ballistic_angle_upper_guess{}; // 0x90, sample 20.0
        float ballistic_angle_lower_guess{}; // 0x94, sample -40.0
        float gravity_scale_guess{};         // 0x98, sample 0.5
        float projectile_lifetime_guess{};   // 0x9C, sample 30.0
        bool uses_ballistics_guess{};        // 0xA0, sample true
        float typical_speed{};               // 0xA4, reflected name TypicalSpeed
        bool inherit_velocity_guess{};       // 0xA8, sample true

        float rate_of_fire_rpm_guess{};         // 0x138, sample 700.0
        std::uint8_t default_fire_mode_guess{}; // 0x150, follows TArray<EWDFireMode>
        float fire_delay_guess{};               // 0x154, sample 0.2
        float recovery_rate_guess{};            // 0x158, sample 1.2
        bool automatic_fire_guess{};            // 0x15C, sample true
        std::int32_t min_zeroing_range{};       // 0x160, reflected name
        std::int32_t max_zeroing_range{};       // 0x164, reflected name
        std::int32_t zeroing_step{};            // 0x168, verified 100 m steps
        bool zeroing_enabled_guess{};           // 0x16C, sample true
        bool clamp_zeroing_guess{};             // 0x16D, sample true

        float aim_curve_scale_guess{};        // 0xB78, sample 1.0
        std::uint8_t fov_shifting_type{};     // 0xC08, EWDWeaponFOVShiftingType
        float fov_shift_scale_guess{};        // 0xC0C, sample 1.5
        float fov_blend_time_guess{};         // 0xC98, sample 0.25
        float firing_animation_angle_guess{}; // 0xCB8, sample 45.0
        float muzzle_velocity{};              // 0xDA0, verified UU/s
        float effective_range{};              // 0xDA4, verified meters
        float suppression_multiplier{};       // 0xDA8, verified multiplier
        float melee_damage{};                 // 0xDAC, reflected name
        float raw_e38{};                      // 0xE38, unknown float
        float raw_e3c{};                      // 0xE3C, unknown float

        FVector muzzle_position{};
        bool muzzle_valid = false;
    };

    bool try_read(void* character_anim_instance, Snapshot& output);
    bool try_read_muzzle(std::uintptr_t pawn, FVector& position);

} // namespace wdgs::weapon_stats
