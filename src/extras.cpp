#include "extras.h"
#include "hook_process_event.h"
#include "visual_math.h"
#include "engine_funcs.h"
#include <unordered_map>

namespace
{
    void* explosive_class = nullptr;
    void* placed_class = nullptr;
    void* container_class = nullptr;
    void* bag_class = nullptr;
    void* vehicle_classes[5]{};
    ULONGLONG next_resolve = 0, next_recoil = 0;
    double server_raw = 0, server_anchor = 0, server_now = 0;
    struct Kind
    {
        FNameValue name{};
        int marker = 0, faction = 0;
    };
    std::unordered_map<std::uintptr_t, Kind> classes;
    struct Label
    {
        std::uintptr_t actor = 0;
        FNameValue name{};
        char text[32]{};
    };
    Label labels[128]{};
    unsigned next_label = 0;
    struct Candidate
    {
        std::uintptr_t actor;
        FNameValue name;
        int kind;
    } candidates[128]{};
    int candidate_count = 0, explosive_count = 0, bag_count = 0;
    ULONGLONG next_discovery = 0;
    bool discover = false;
    struct Recoil
    {
        std::uintptr_t owner = 0, stats = 0;
        FNameValue name{};
        float values[6]{};
    } saved;
    // Verified Release offsets from spot/SDK/Offsets.cpp; use the same six fields.
    constexpr std::uintptr_t recoil_fields[]{0xB78, 0xCB8, 0xCC0, 0xCC4, 0xCD0, 0xCD4};

    float marker_range(const Config& settings, bool bag)
    {
        float range = bag ? (settings.extra.death_bags ? settings.extra.bag_range : 0.f) : (settings.extra.explosives ? settings.extra.explosive_range : 0.f);
        if (bag ? settings.radar.bags : settings.radar.explosives)
            range = std::max(range, radar_scan_range(settings));
        return range;
    }

    bool same_name(FNameValue a, FNameValue b)
    {
        return a.ComparisonIndex == b.ComparisonIndex && a.Number == b.Number;
    }

    void restore_recoil()
    {
        if (saved.owner && engine::is_live_object(reinterpret_cast<void*>(saved.owner)) && same_name(saved.name, read<FNameValue>(saved.owner + offsets::UObject::NamePrivate)))
            for (int i = 0; i < 6; ++i)
                if (read<float>(saved.stats + recoil_fields[i]) == 0)
                    write<float>(saved.stats + recoil_fields[i], saved.values[i]);
        saved = {};
    }

    Kind classify(std::uintptr_t actor)
    {
        const auto cls = read<std::uintptr_t>(actor + offsets::UObject::ClassPrivate);
        if (!engine::is_live_object(reinterpret_cast<void*>(cls)))
            return {};
        const auto name = read<FNameValue>(cls + offsets::UObject::NamePrivate);
        const auto found = classes.find(cls);
        if (found != classes.end() && same_name(found->second.name, name))
            return found->second;
        Kind kind{name};
        if (extras::derives(actor, explosive_class))
            kind.marker = 1;
        else if (extras::derives(actor, placed_class))
            kind.marker = 2;
        else if (extras::derives(actor, container_class))
            kind.marker = extras::derives(actor, bag_class) ? 4 : 3;
        constexpr int fields[]{0xAC8, 0xBA8, 0xB88, 0xBE8, 0xBD8};
        for (int i = 0; i < 5; ++i)
            if (extras::derives(actor, vehicle_classes[i]))
            {
                kind.faction = fields[i];
                break;
            }
        if (classes.size() < 512)
            classes.insert_or_assign(cls, kind);
        return kind;
    }

    const char* marker_label(std::uintptr_t actor, int kind)
    {
        const auto name = read<FNameValue>(actor + offsets::UObject::NamePrivate);
        for (auto& label : labels)
            if (label.actor == actor && same_name(label.name, name))
                return label.text;
        auto& label = labels[next_label++ % 128];
        label = {};
        label.actor = actor;
        label.name = name;
        strcpy_s(label.text, kind == 1 ? "GRENADE" : kind == 2 ? "PLACED EXPLOSIVE"
                                                 : kind == 4   ? "d-bag"
                                                               : "CONTAINER");
        if (kind <= 2)
        {
            const auto tag = read<FNameValue>(actor + (kind == 1 ? 0x4B8 : 0x368));
            FString text = engine_funcs::conv_name_to_string(tag);
            const auto str = text.ToWString(128);
            const char* word = nullptr;
            if (str.find(L"Smoke") != std::wstring::npos)
                word = "SMOKE";
            else if (str.find(L"C4") != std::wstring::npos || str.find(L"IED") != std::wstring::npos)
                word = "C4";
            else if (str.find(L"Claymore") != std::wstring::npos)
                word = "CLAYMORE";
            else if (str.find(L"Mine") != std::wstring::npos)
                word = "MINE";
            if (word)
                strcpy_s(label.text, word);
            engine_funcs::release_string(text);
        }
        return label.text;
    }
} // namespace

bool extras::derives(std::uintptr_t object, void* type)
{
    if (!type || !engine::is_live_object(reinterpret_cast<void*>(object)))
        return false;
    auto cls = read<std::uintptr_t>(object + offsets::UObject::ClassPrivate);
    for (int i = 0; cls && i < 16; ++i)
    {
        if (cls == reinterpret_cast<std::uintptr_t>(type))
            return true;
        if (!engine::is_live_object(reinterpret_cast<void*>(cls)))
            return false;
        cls = read<std::uintptr_t>(cls + offsets::UStruct::SuperStruct);
    }
    return false;
}

void extras::reset()
{
    restore_recoil();
    explosive_class = placed_class = container_class = bag_class = nullptr;
    for (auto& cls : vehicle_classes)
        cls = nullptr;
    classes.clear();
    for (auto& label : labels)
        label = {};
    next_label = 0;
    next_resolve = next_recoil = next_discovery = 0;
    candidate_count = 0;
    discover = false;
    server_raw = server_anchor = server_now = 0;
}

void extras::begin(std::uintptr_t world, const Config& settings)
{
    const bool explosives = marker_range(settings, false) > 0;
    const bool bags = marker_range(settings, true) > 0;
    const bool markers = explosives || bags;
    const bool vehicles = settings.aimbot.enabled && (settings.aimbot.silent_aim || settings.aimbot.magic_bullet);
    discover = markers && frame_ticks >= next_discovery;
    if (discover)
    {
        candidate_count = explosive_count = bag_count = 0;
        next_discovery = frame_ticks + 100;
    }
    if (!markers)
        candidate_count = 0;
    if (!markers && !vehicles)
        return;
    if (frame_ticks >= next_resolve && ((explosives && (!explosive_class || !placed_class)) || (bags && (!container_class || !bag_class)) || (vehicles && (!vehicle_classes[0] || !vehicle_classes[1] || !vehicle_classes[2] || !vehicle_classes[3] || !vehicle_classes[4]))))
    {
        next_resolve = frame_ticks + 5000;
        const auto resolve = [](void*& dst, const wchar_t* name)
        { if (!dst) dst = engine::static_find_object(nullptr, nullptr, name); };
        if (explosives)
        {
            resolve(explosive_class, L"/Script/WDGame.WDExplosive");
            resolve(placed_class, L"/Script/WDGame.WDPlaceable");
        }
        if (bags)
        {
            resolve(container_class, L"/Script/WDGame.WDContainer");
            resolve(bag_class, L"/Script/WDGame.WDPlayerInventoryContainer");
        }
        if (vehicles)
        {
            const wchar_t* names[]{L"/Script/WDGame.WDStationaryVehicle", L"/Script/WDGame.WDAirplaneVehicle", L"/Script/WDGame.WDRotaryVehicle", L"/Script/WDGame.WDTrackedVehicle", L"/Script/WDGame.WDWheeledVehiclePawn"};
            for (int i = 0; i < 5; ++i)
                resolve(vehicle_classes[i], names[i]);
        }
        classes.clear();
    }
    server_now = 0;
    if (!explosives)
        return;
    const auto state = read<std::uintptr_t>(world + offsets::World::GameState);
    if (!engine::is_live_object(reinterpret_cast<void*>(state)))
        return;
    const double raw = read<double>(state + 0x2E8);
    if (std::isfinite(raw) && raw > 0 && raw < 1e8)
    {
        if (raw != server_raw)
        {
            server_raw = raw;
            server_anchor = frame_time;
        }
        if (frame_time >= server_anchor && frame_time - server_anchor < 30)
            server_now = raw + frame_time - server_anchor;
    }
}

void extras::collect(std::uintptr_t actor, const CameraIPC& camera, const Config& settings, game::Snapshot&)
{
    if (!discover || candidate_count >= 128)
        return;
    const int kind = classify(actor).marker;
    const float range = marker_range(settings, kind >= 3);
    if (!kind || range <= 0)
        return;
    int& count = kind <= 2 ? explosive_count : bag_count;
    if (count >= 64)
        return;
    const auto root = read<std::uintptr_t>(actor + offsets::AActor::RootComponent);
    if (!engine::is_live_object(reinterpret_cast<void*>(root)))
        return;
    const auto transform = read<FTransform>(root + offsets::USceneComponent::ComponentToWorld);
    if (!visual_math::finite(transform.Translation) || transform.Translation.Distance(camera.location) > 100 * range)
        return;
    ++count;
    candidates[candidate_count++] = {actor, read<FNameValue>(actor + offsets::UObject::NamePrivate), kind};
}

static void collect_marker(std::uintptr_t actor, int kind, const CameraIPC& camera, const Config& settings, game::Snapshot& output)
{
    const float range = marker_range(settings, kind >= 3);
    if (range <= 0)
        return;
    const auto root = read<std::uintptr_t>(actor + offsets::AActor::RootComponent);
    if (!engine::is_live_object(reinterpret_cast<void*>(root)))
        return;
    const auto transform = read<FTransform>(root + offsets::USceneComponent::ComponentToWorld);
    if (!visual_math::finite(transform.Translation))
        return;
    game::Snapshot::Marker marker;
    marker.actor = actor;
    marker.world = transform.Translation;
    marker.distance = static_cast<float>(marker.world.Distance(camera.location) * 0.01);
    marker.bag = kind >= 3;
    if (!std::isfinite(marker.distance) || marker.distance > range)
        return;
    const bool label_needed = marker.bag ? settings.extra.death_bags && marker.distance <= settings.extra.bag_range : settings.extra.explosives && marker.distance <= settings.extra.explosive_range;
    if (kind == 1)
    {
        const float thrown = read<float>(actor + 0x318), explode = read<float>(actor + 0x340);
        if (!std::isfinite(thrown) || !std::isfinite(explode))
            return;
        if (server_now > 0 && explode > 0)
        {
            if (explode - server_now <= -0.15)
                return;
            marker.fuse_left = std::max(0.f, static_cast<float>(explode - server_now));
            marker.fuse_total = explode - thrown;
            marker.timed = marker.fuse_total > 0.05f && marker.fuse_total < 120 && marker.fuse_left < 120;
        }
    }
    if (marker.bag && label_needed)
    {
        const auto nearby = read<TArray<std::uintptr_t>>(actor + 0x2F0);
        if (nearby.IsSane(32))
            marker.nearby = nearby.Count;
    }
    if (label_needed)
        strcpy_s(marker.label, marker_label(actor, kind));
    output.markers.push_back(marker);
}

void extras::finish(const CameraIPC& camera, const Config& settings, game::Snapshot& output)
{
    for (int i = 0; i < candidate_count; ++i)
    {
        const auto& candidate = candidates[i];
        if (marker_range(settings, candidate.kind >= 3) <= 0)
            continue;
        if (engine::is_live_object(reinterpret_cast<void*>(candidate.actor)) && same_name(candidate.name, read<FNameValue>(candidate.actor + offsets::UObject::NamePrivate)))
            collect_marker(candidate.actor, candidate.kind, camera, settings, output);
    }
}

bool extras::vehicle_team(std::uintptr_t actor, void* local_faction, bool& teammate)
{
    const int offset = classify(actor).faction;
    if (!offset || !local_faction)
        return false;
    const auto component = read<std::uintptr_t>(actor + offset);
    if (!engine::is_live_object(reinterpret_cast<void*>(component)))
        return false;
    const auto faction = read<std::uintptr_t>(component + offsets::UWDFactionComponent::FactionObject);
    if (!engine::is_live_object(reinterpret_cast<void*>(faction)))
        return false;
    teammate = faction == reinterpret_cast<std::uintptr_t>(local_faction);
    return true;
}

void extras::recoil(const wdgs::weapon_stats::Snapshot& weapon, bool enabled)
{
    if (!enabled || weapon.stats_data != saved.owner || weapon.stats != saved.stats || (saved.owner && !same_name(saved.name, read<FNameValue>(saved.owner + offsets::UObject::NamePrivate))))
        restore_recoil();
    if (!enabled || !engine::is_live_object(reinterpret_cast<void*>(weapon.stats_data)))
        return;
    if (!saved.owner)
    {
        Recoil next{weapon.stats_data, weapon.stats, read<FNameValue>(weapon.stats_data + offsets::UObject::NamePrivate)};
        if (!next.name.ComparisonIndex)
            return;
        for (int i = 0; i < 6; ++i)
            if (!read_mem(next.stats + recoil_fields[i], &next.values[i], sizeof(float)) || !std::isfinite(next.values[i]))
                return;
        saved = next;
    }
    if (frame_ticks < next_recoil)
        return;
    next_recoil = frame_ticks + 16;
    for (auto field : recoil_fields)
        write<float>(saved.stats + field, 0.f);
}
