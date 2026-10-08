#include "stdafx.h"
#include "player_snapshot.h"
#include "offsets.h"
#include "classes.h"
#include "engine_funcs.h"
#include "mortar_aim.h"

#include <cmath>
#include <cwctype>
#include <unordered_map>

namespace
{

    struct BoneFNameCache
    {
        FNameValue fnames[BONE_COUNT]{};
        bool initialized = false;
    };

    struct BoneCacheEntry
    {
        BoneFNameCache bones{};
        FNameValue asset_name{};
        std::uint64_t last_seen_frame = 0;
        int next_index = 0;
        int found_count = 0;
        bool found[BONE_COUNT]{};
    };

    struct PlayerNameCacheEntry
    {
        std::uintptr_t player_state = 0;
        FNameValue actor_name{};
        std::uint64_t last_seen_frame = 0;
        wchar_t name[32]{};
    };

    // Character assets can reorder the same semantic bones, especially when a
    // helmet/customized mesh is attached. Resolve names once per mesh instead of
    // sharing a fragile process-wide index table.
    constexpr int kMaxBoneScan = 512;
    constexpr const wchar_t* kBoneNames[BONE_COUNT] = {
        L"head",
        L"neck_01",
        L"spine_03",
        L"pelvis",
        L"upperarm_l",
        L"lowerarm_l",
        L"hand_l",
        L"upperarm_r",
        L"lowerarm_r",
        L"hand_r",
        L"thigh_l",
        L"calf_l",
        L"foot_l",
        L"thigh_r",
        L"calf_r",
        L"foot_r",
        L"clavicle_l",
        L"clavicle_r",
        L"root",
    };

    constexpr std::uint64_t kCachePruneInterval = 300;
    constexpr std::uint64_t kCacheRetainFrames = 900;
    std::unordered_map<std::uintptr_t, BoneCacheEntry> g_bone_caches;
    std::unordered_map<std::uintptr_t, PlayerNameCacheEntry> g_player_names;
    std::uint64_t g_cache_frame = 1;
    int g_bone_scan_budget = 0;

    std::wstring normalize_bone_name(const std::wstring& input)
    {
        std::wstring result = input;
        for (wchar_t& character : result)
            character = static_cast<wchar_t>(std::towlower(character));
        return result;
    }

    void advance_bone_cache(BoneCacheEntry& entry, void* mesh)
    {
        while (entry.next_index < kMaxBoneScan && entry.found_count < BONE_COUNT && g_bone_scan_budget > 0)
        {
            const int bone_index = entry.next_index++;
            --g_bone_scan_budget;
            const FNameValue fname = engine_funcs::get_bone_name(mesh, bone_index);
            if (fname.ComparisonIndex == 0)
                continue;

            // The mesh root is bone zero, regardless of its asset-specific name.
            if (bone_index == 0)
            {
                entry.bones.fnames[BONE_ROOT] = fname;
                entry.found[BONE_ROOT] = true;
                ++entry.found_count;
            }

            FString name = engine_funcs::conv_name_to_string(fname);
            if (!name.IsValid())
                continue;

            const std::wstring normalized = normalize_bone_name(name.ToWString(64));
            engine_funcs::release_string(name);
            if (normalized.empty())
                continue;

            for (int slot = 0; slot < BONE_COUNT; ++slot)
            {
                if (entry.found[slot] || normalized != kBoneNames[slot])
                    continue;
                entry.bones.fnames[slot] = fname;
                entry.found[slot] = true;
                ++entry.found_count;
                break;
            }
        }

        // Clavicles are optional on a few stripped meshes; the core body chain is
        // required so a partially streamed asset never publishes stray lines.
        const int body_bones = entry.found_count - (entry.found[BONE_ROOT] ? 1 : 0);
        entry.bones.initialized = body_bones >= BONE_ROOT - 2;
    }

    const BoneFNameCache* get_bone_cache(void* mesh)
    {
        const auto mesh_address = reinterpret_cast<std::uintptr_t>(mesh);
        const auto asset = read<std::uintptr_t>(mesh_address + offsets::WDSkeletalMeshComponentBudgeted::SkinnedAsset);
        if (!is_valid_ptr(reinterpret_cast<void*>(asset)))
            return nullptr;

        const FNameValue asset_name = read<FNameValue>(asset + offsets::UObject::NamePrivate);
        auto existing = g_bone_caches.find(asset);
        if (existing != g_bone_caches.end())
        {
            BoneCacheEntry& entry = existing->second;
            if (entry.asset_name.ComparisonIndex == asset_name.ComparisonIndex && entry.asset_name.Number == asset_name.Number)
            {
                entry.last_seen_frame = g_cache_frame;
                if (!entry.bones.initialized && entry.next_index < kMaxBoneScan)
                    advance_bone_cache(entry, mesh);
                if (!entry.bones.initialized)
                    return nullptr;
                return &entry.bones;
            }
            g_bone_caches.erase(existing);
        }

        BoneCacheEntry entry{};
        entry.asset_name = asset_name;
        entry.last_seen_frame = g_cache_frame;
        auto inserted = g_bone_caches.emplace(asset, std::move(entry));
        advance_bone_cache(inserted.first->second, mesh);
        return inserted.first->second.bones.initialized
                   ? &inserted.first->second.bones
                   : nullptr;
    }

    bool read_player_transform(std::uintptr_t actor, std::uintptr_t& root_component, FTransform& transform)
    {
        root_component = read<std::uintptr_t>(actor + offsets::AActor::RootComponent);
        if (!is_valid_ptr(reinterpret_cast<void*>(root_component)))
            return false;

        transform = read<FTransform>(root_component + offsets::USceneComponent::ComponentToWorld);
        return std::isfinite(transform.Translation.X) &&
               std::isfinite(transform.Translation.Y) &&
               std::isfinite(transform.Translation.Z);
    }

    void read_player_name(std::uintptr_t actor, FNameValue actor_name, std::uintptr_t player_state, Player& player)
    {
        if (!player_state)
            return;

        auto cached = g_player_names.find(actor);
        if (cached != g_player_names.end() && cached->second.player_state == player_state && cached->second.actor_name.ComparisonIndex == actor_name.ComparisonIndex && cached->second.actor_name.Number == actor_name.Number && cached->second.name[0] != L'\0')
        {
            cached->second.last_seen_frame = g_cache_frame;
            wcsncpy_s(player.player_name, cached->second.name, _TRUNCATE);
            return;
        }

        const auto state = reinterpret_cast<APlayerState*>(player_state);
        const std::wstring name = state->PlayerName();
        wcsncpy_s(player.player_name, name.c_str(), 31);
        if (player.player_name[0] == L'\0')
            return;

        PlayerNameCacheEntry entry{};
        entry.player_state = player_state;
        entry.actor_name = actor_name;
        entry.last_seen_frame = g_cache_frame;
        wcsncpy_s(entry.name, player.player_name, _TRUNCATE);
        g_player_names.insert_or_assign(actor, std::move(entry));
    }

    void read_health(std::uintptr_t actor, Player& player)
    {
        const auto vitality = engine_funcs::get_vitality_component(reinterpret_cast<void*>(actor));
        if (!vitality)
            return;

        player.max_health = engine_funcs::get_max_health(vitality);
        player.health = engine_funcs::get_current_health(vitality);
        if (!std::isfinite(player.max_health) || player.max_health < 0.f)
            player.max_health = 0.f;
        if (!std::isfinite(player.health))
            player.health = 0.f;
    }

    void read_bones(std::uintptr_t actor, Player& player, bool full_skeleton, bool box)
    {
        const auto mesh = read<std::uintptr_t>(actor + offsets::ACharacter::Mesh);
        if (!is_valid_ptr(reinterpret_cast<void*>(mesh)) || !engine_funcs::bone_functions_ready())
            return;

        void* mesh_ptr = reinterpret_cast<void*>(mesh);
        const BoneFNameCache* bone_cache = get_bone_cache(mesh_ptr);
        if (!bone_cache)
            return;

        int valid_count = 0;
        for (int slot = 0; slot < BONE_COUNT; ++slot)
        {
            // Read the central chain and optional box root. Arms and legs are
            // only needed for a nearby player's skeleton.
            if (slot == BONE_ROOT ? !box : !full_skeleton && slot > BONE_PELVIS)
                continue;
            const FNameValue& fname = bone_cache->fnames[slot];
            if (fname.ComparisonIndex == 0)
                continue;

            const FVector location = engine_funcs::get_socket_location(mesh_ptr, fname);
            if (!std::isfinite(location.X) || !std::isfinite(location.Y) || !std::isfinite(location.Z))
                continue;

            player.bones[slot] = location;
            if (location.X != 0.0 || location.Y != 0.0 || location.Z != 0.0)
                ++valid_count;
        }

        const bool core_bones_valid =
            player.bones[BONE_HEAD].Length() > 1.0 &&
            player.bones[BONE_NECK].Length() > 1.0 &&
            player.bones[BONE_CHEST].Length() > 1.0 &&
            player.bones[BONE_PELVIS].Length() > 1.0;
        // The distance fast path intentionally reads only the central chain.
        // Keep it aim-eligible while still requiring the full set for skeleton ESP.
        player.has_bones = core_bones_valid &&
                           (!full_skeleton || valid_count >= 8);
    }

    void read_mortar_state(std::uintptr_t actor, std::uintptr_t root_component, Player& player)
    {
        const std::uintptr_t attach_parent = read<std::uintptr_t>(root_component + offsets::USceneComponent::AttachParent);
        if (!is_valid_ptr(reinterpret_cast<void*>(attach_parent)))
            return;

        const std::uintptr_t owner = read<std::uintptr_t>(attach_parent + offsets::UObject::OuterPrivate);
        if (is_valid_ptr(reinterpret_cast<void*>(owner)) && owner != actor)
            player.is_on_mortar = wdgs::mortar_aim::vehicle_is_mortar(owner);
    }

    bool read_vehicle_state(std::uintptr_t actor)
    {
        const std::uintptr_t operator_component = read<std::uintptr_t>(actor + offsets::AWDMoverCharacter::VehicleOperator);
        if (!is_valid_ptr(reinterpret_cast<void*>(operator_component)))
            return false;

        std::uintptr_t seat = read<std::uintptr_t>(operator_component + offsets::UWDVehicleOperatorComponent::CurrentSeat);
        if (!is_valid_ptr(reinterpret_cast<void*>(seat)))
            seat = read<std::uintptr_t>(operator_component + offsets::UWDVehicleOperatorComponent::ReplicatedSeat);
        if (!is_valid_ptr(reinterpret_cast<void*>(seat)))
            return false;

        const std::uintptr_t vehicle = read<std::uintptr_t>(seat + offsets::UObject::OuterPrivate);
        return is_valid_ptr(reinterpret_cast<void*>(vehicle));
    }

} // namespace

void wdgs::snapshot::begin_frame()
{
    ++g_cache_frame;
    g_bone_scan_budget = 96;
    if (g_cache_frame % kCachePruneInterval != 0)
        return;

    for (auto it = g_bone_caches.begin(); it != g_bone_caches.end();)
    {
        if (g_cache_frame - it->second.last_seen_frame > kCacheRetainFrames)
            it = g_bone_caches.erase(it);
        else
            ++it;
    }

    for (auto it = g_player_names.begin(); it != g_player_names.end();)
    {
        if (g_cache_frame - it->second.last_seen_frame > kCacheRetainFrames)
            it = g_player_names.erase(it);
        else
            ++it;
    }
}

void wdgs::snapshot::reset_caches()
{
    g_bone_caches.clear();
    g_bone_caches.reserve(64);
    g_player_names.clear();
    g_player_names.reserve(128);
    g_cache_frame = 1;
    g_bone_scan_budget = 96;
}

void* wdgs::snapshot::GetFaction(std::uintptr_t player_state)
{
    if (!player_state)
        return nullptr;
    return engine_funcs::get_faction(reinterpret_cast<void*>(player_state));
}

bool wdgs::snapshot::TryBuildPlayer(std::uintptr_t actor, const ActorSnapshotContext& context, Player& player, std::uintptr_t& root_component)
{
    // game::tick validates the actor before entering this hot path.
    const FNameValue actor_name = read<FNameValue>(actor + offsets::UObject::NamePrivate);
    // The caller first selects actors by the resolved class name and falls
    // back to the PlayerState::PawnPrivate ownership link when FName decoding
    // is temporarily unavailable.  Keep the ownership check below as the
    // final gate so downed players remain visible while surrendered actors
    // are dropped immediately.

    FTransform transform{};
    if (!read_player_transform(actor, root_component, transform))
        return false;

    const std::uintptr_t player_state = read<std::uintptr_t>(actor + offsets::APawn::PlayerState);
    const bool valid_player_state =
        is_valid_ptr(reinterpret_cast<void*>(player_state));
    if (!valid_player_state)
        return false;

    // A pawn remains in the actor array while it is downed and can still be
    // reanimated.  During that state PlayerState::PawnPrivate continues to
    // point at this pawn, so it is rendered with the existing zero-health
    // (purple) presentation.  Giving up clears/reassigns PawnPrivate; using
    // this ownership link stops publishing the stale actor immediately while
    // avoiding a health-value guess for the surrender transition.
    const std::uintptr_t owned_pawn = read<std::uintptr_t>(player_state + offsets::APlayerState::PawnPrivate);
    if (owned_pawn != actor)
        return false;

    const void* remote_faction = valid_player_state
                                     ? GetFaction(player_state)
                                     : nullptr;

    player = {};
    player.world_pos = transform.Translation;
    const auto& q = transform.Rotation;
    const double yaw = std::atan2(2 * (q.W * q.Z + q.X * q.Y), 1 - 2 * (q.Y * q.Y + q.Z * q.Z)) * 57.29577951308232;
    player.yaw_valid = std::isfinite(yaw);
    player.yaw = player.yaw_valid ? static_cast<float>(yaw) : 0;
    player.distance = static_cast<float>(context.observer_position.Distance(player.world_pos) / 100.0);
    if (!std::isfinite(player.distance) || player.distance < 0.f)
        return false;

    player.is_in_team = context.local_faction != nullptr &&
                        remote_faction != nullptr && context.local_faction == remote_faction;
    player.is_in_vehicle = read_vehicle_state(actor);
    player.isVisible = true;

    if (valid_player_state)
        read_player_name(actor, actor_name, player_state, player);
    read_health(actor, player);
    return true;
}

void wdgs::snapshot::PopulatePlayerBones(std::uintptr_t actor, Player& player, bool full_skeleton, bool box)
{
    read_bones(actor, player, full_skeleton, box);
}

void wdgs::snapshot::PopulatePlayerMortarState(std::uintptr_t actor, std::uintptr_t root_component, Player& player)
{
    read_mortar_state(actor, root_component, player);
}
