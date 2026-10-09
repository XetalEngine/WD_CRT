#include "stdafx.h"
#include "weapon_stats.h"

#include "engine_funcs.h"
#include "offsets.h"

#include <cmath>

bool wdgs::weapon_stats::try_read(void* character_anim_instance, Snapshot& output)
{
    output = {};
    if (!is_valid_ptr(character_anim_instance))
        return false;

    void* behavior = engine_funcs::get_weapon_behavior_component(character_anim_instance);
    if (!is_valid_ptr(behavior))
        return false;

    const auto behavior_address = reinterpret_cast<std::uintptr_t>(behavior);
    const std::uintptr_t stats_data = read<std::uintptr_t>(behavior_address + offsets::UWDWeaponBehaviorComponent::WeaponStatsData);
    if (!is_valid_ptr(reinterpret_cast<void*>(stats_data)))
        return false;

    if (stats_data > (std::numeric_limits<std::uintptr_t>::max)() - offsets::UWDWeaponStatsData::WeaponStats)
        return false;

    const std::uintptr_t stats = stats_data + offsets::UWDWeaponStatsData::WeaponStats;
    if (!is_valid_ptr(reinterpret_cast<void*>(stats + offsets::FWDWeaponStats::TypicalSpeed), sizeof(float)) || !is_valid_ptr(reinterpret_cast<void*>(stats + offsets::FWDWeaponStats::FiringAnimationAngleGuess), sizeof(float)))
        return false;

    output.behavior_component = behavior_address;
    output.stats_data = stats_data;
    output.stats = stats;

    output.ballistic_angle_upper_guess = read<float>(stats + offsets::FWDWeaponStats::BallisticAngleUpperGuess);
    output.ballistic_angle_lower_guess = read<float>(stats + offsets::FWDWeaponStats::BallisticAngleLowerGuess);
    output.gravity_scale_guess = read<float>(stats + offsets::FWDWeaponStats::GravityScaleGuess);
    output.projectile_lifetime_guess = read<float>(stats + offsets::FWDWeaponStats::ProjectileLifetimeGuess);
    output.uses_ballistics_guess = read<bool>(stats + offsets::FWDWeaponStats::UsesBallisticsGuess);
    output.typical_speed = read<float>(stats + offsets::FWDWeaponStats::TypicalSpeed);
    output.inherit_velocity_guess = read<bool>(stats + offsets::FWDWeaponStats::InheritVelocityGuess);
    output.rate_of_fire_rpm_guess = read<float>(stats + offsets::FWDWeaponStats::RateOfFireRpmGuess);
    output.default_fire_mode_guess = read<std::uint8_t>(stats + offsets::FWDWeaponStats::DefaultFireModeGuess);
    output.fire_delay_guess = read<float>(stats + offsets::FWDWeaponStats::FireDelayGuess);
    output.recovery_rate_guess = read<float>(stats + offsets::FWDWeaponStats::RecoveryRateGuess);
    output.automatic_fire_guess = read<bool>(stats + offsets::FWDWeaponStats::AutomaticFireGuess);
    output.min_zeroing_range = read<std::int32_t>(stats + offsets::FWDWeaponStats::MinZeroingRange);
    output.max_zeroing_range = read<std::int32_t>(stats + offsets::FWDWeaponStats::MaxZeroingRange);
    output.zeroing_step = read<std::int32_t>(stats + offsets::FWDWeaponStats::ZeroingStep);
    output.zeroing_enabled_guess = read<bool>(stats + offsets::FWDWeaponStats::ZeroingEnabledGuess);
    output.clamp_zeroing_guess = read<bool>(stats + offsets::FWDWeaponStats::ClampZeroingGuess);
    output.aim_curve_scale_guess = read<float>(stats + offsets::FWDWeaponStats::AimCurveScaleGuess);
    output.fov_shifting_type = read<std::uint8_t>(stats + offsets::FWDWeaponStats::FovShiftingType);
    output.fov_shift_scale_guess = read<float>(stats + offsets::FWDWeaponStats::FovShiftScaleGuess);
    output.fov_blend_time_guess = read<float>(stats + offsets::FWDWeaponStats::FovBlendTimeGuess);
    output.firing_animation_angle_guess = read<float>(stats + offsets::FWDWeaponStats::FiringAnimationAngleGuess);
    output.muzzle_velocity = read<float>(stats + offsets::FWDWeaponStats::MuzzleVelocity);
    output.effective_range = read<float>(stats + offsets::FWDWeaponStats::EffectiveRange);
    output.suppression_multiplier = read<float>(stats + offsets::FWDWeaponStats::SuppressionMultiplier);
    output.melee_damage = read<float>(stats + offsets::FWDWeaponStats::MeleeDamage);
    output.raw_e38 = read<float>(stats + offsets::FWDWeaponStats::RawE38);
    output.raw_e3c = read<float>(stats + offsets::FWDWeaponStats::RawE3C);

    // A valid stats object may deliberately expose TypicalSpeed as -1.  Keep
    // the snapshot available so every field can be inspected independently.
    return true;
}

static bool is_live_uobject(std::uintptr_t obj)
{
    if (!is_valid_ptr(reinterpret_cast<void*>(obj)))
        return false;
    auto vtable = read<std::uintptr_t>(obj + offsets::UObject::VTable);
    auto cls = read<std::uintptr_t>(obj + offsets::UObject::ClassPrivate);
    return is_valid_ptr(reinterpret_cast<void*>(vtable)) &&
           is_valid_ptr(reinterpret_cast<void*>(cls));
}

bool wdgs::weapon_stats::try_read_muzzle(std::uintptr_t pawn, FVector& position)
{
    static FNameValue s_muzzle_fname{};
    if (s_muzzle_fname.ComparisonIndex == 0)
        s_muzzle_fname = engine_funcs::conv_string_to_name(FString(xor_text(L"S_Muzzle")));
    if (s_muzzle_fname.ComparisonIndex == 0 || !is_valid_ptr(reinterpret_cast<void*>(pawn)))
        return false;

    constexpr std::uintptr_t kInstComps = 0x290;
    TArray<std::uintptr_t> child_actors = read<TArray<std::uintptr_t>>(pawn + offsets::AActor::Children);
    if (!child_actors.IsSane(32) || child_actors.Num() <= 0)
        return false;

    for (int i = 0; i < child_actors.Num(); i++)
    {
        std::uintptr_t ca = child_actors[i];
        if (!is_live_uobject(ca))
            continue;
        TArray<std::uintptr_t> inst_comps = read<TArray<std::uintptr_t>>(ca + kInstComps);
        if (!inst_comps.IsSane(64) || inst_comps.Num() <= 0)
            continue;
        for (int j = 0; j < inst_comps.Num(); j++)
        {
            std::uintptr_t ic = inst_comps[j];
            if (!is_live_uobject(ic))
                continue;
            std::uintptr_t asset = read<std::uintptr_t>(ic + offsets::WDSkeletalMeshComponentBudgeted::SkinnedAsset);
            if (!is_valid_ptr(reinterpret_cast<void*>(asset)))
                continue;
            TArray<std::uintptr_t> sockets = read<TArray<std::uintptr_t>>(asset + offsets::USkeletalMesh::Sockets);
            if (!sockets.IsSane(128) || sockets.Num() <= 0)
                continue;
            for (int k = 0; k < sockets.Num(); k++)
            {
                std::uintptr_t sock = sockets[k];
                if (!is_valid_ptr(reinterpret_cast<void*>(sock)))
                    continue;
                FNameValue sn = read<FNameValue>(sock + offsets::UObject::NamePrivate);
                if (sn.ComparisonIndex != s_muzzle_fname.ComparisonIndex)
                    continue;
                FVector loc = engine_funcs::get_socket_location(reinterpret_cast<void*>(ic), s_muzzle_fname);
                if (std::isfinite(loc.X) && std::isfinite(loc.Y) && std::isfinite(loc.Z) && loc.Length() > 1.0)
                {
                    position = loc;
                    return true;
                }
                return false;
            }
        }
    }
    return false;
}
