#include "../src/config.h"
#include "../src/aimbot.h"
#include "../src/extras.h"
#include "../src/tracers.h"
#include "../src/magic_bullet.h"
#include "../src/game_actions.h"
#include "../src/hook_process_event.h"
#include "../src/visual_math.h"
#include "../src/overlay.h"
#include "../src/visuals.h"
#include "../src/menu.h"

namespace
{
    struct Object
    {
        alignas(16) unsigned char bytes[0x1800]{};
        Object()
        {
            put(offsets::UObject::VTable, addr());
            put(offsets::UObject::ClassPrivate, addr());
            put(offsets::UObject::NamePrivate, FNameValue{1, 0});
        }
        std::uintptr_t addr() const
        {
            return reinterpret_cast<std::uintptr_t>(bytes);
        }
        template <class T>
        void put(std::size_t at, T value)
        {
            memcpy(bytes + at, &value, sizeof(value));
        }
    };
    Object session, state_type, manager_type, tool_type, decal_type, explosive_type, container_type;
    Object component_fn, eyes_fn, location_fn, trace_fn, reserve_fn, commit_fn, string_fn, library, get_rot_fn, set_rot_fn;
    std::uintptr_t pawn_address, item_address, manager_address, tool_address;
    int moved, traces, reserved, committed, rotated, event_calls;

    void* __fastcall find_object(void*, void*, const FString* name, bool)
    {
        const auto s = name->ToWString(200);
        Object* out = nullptr;
        if (s.find(L"WDGameStateSession") != s.npos)
            out = &session;
        else if (s.find(L"Server_ReserveFaction") != s.npos)
            out = &reserve_fn;
        else if (s.find(L"Server_CommitFaction") != s.npos)
            out = &commit_fn;
        else if (s.find(L"WDPlayerStateSession") != s.npos)
            out = &state_type;
        else if (s.find(L"WDPawnBuildableManager") != s.npos)
            out = &manager_type;
        else if (s.find(L"WDItemExtension_BuildBuildable") != s.npos)
            out = &tool_type;
        else if (s.find(L"DecalComponent") != s.npos)
            out = &decal_type;
        else if (s.find(L"GetComponentByClass") != s.npos)
            out = &component_fn;
        else if (s.find(L"GetActorEyesViewPoint") != s.npos)
            out = &eyes_fn;
        else if (s.find(L"K2_SetWorldLocation") != s.npos)
            out = &location_fn;
        else if (s.find(L"LineTraceSingle") != s.npos)
            out = &trace_fn;
        else if (s.find(L"Conv_StringToName") != s.npos)
            out = &string_fn;
        else if (s.find(L"Default__KismetStringLibrary") != s.npos)
            out = &library;
        else if (s.find(L"GetControlRotation") != s.npos)
            out = &get_rot_fn;
        else if (s.find(L"SetControlRotation") != s.npos)
            out = &set_rot_fn;
        else if (s.find(L"WDExplosive") != s.npos)
            out = &explosive_type;
        else if (s.find(L"WDContainer") != s.npos)
            out = &container_type;
        return out ? out->bytes : nullptr;
    }

    void __fastcall event(void* object, void* function, void* params)
    {
        ++event_calls;
        const auto obj = reinterpret_cast<std::uintptr_t>(object);
        auto bytes = static_cast<unsigned char*>(params);
        if (function == component_fn.bytes)
        {
            std::uintptr_t result = obj == pawn_address ? manager_address : obj == item_address ? tool_address
                                                                                                : 0;
            memcpy(bytes + 8, &result, 8);
        }
        else if (function == eyes_fn.bytes)
        {
            const FVector eye{100, 200, 300};
            memcpy(bytes, &eye, sizeof(eye));
        }
        else if (function == trace_fn.bytes)
        {
            const float fraction = 0.4f;
            memcpy(bytes + 0x5C, &fraction, 4);
            bytes[0x180] = 1;
            ++traces;
        }
        else if (function == location_fn.bytes)
        {
            memcpy(reinterpret_cast<void*>(obj + 0x230), bytes, sizeof(FVector));
            ++moved;
        }
        else if (function == string_fn.bytes)
        {
            const FNameValue tag{777, 0};
            memcpy(bytes + sizeof(FString), &tag, sizeof(tag));
        }
        else if (function == reserve_fn.bytes)
            ++reserved;
        else if (function == commit_fn.bytes)
            ++committed;
        else if (function == get_rot_fn.bytes)
        {
            const FRotator rotation{0, 20, 0};
            memcpy(bytes, &rotation, sizeof(rotation));
        }
        else if (function == set_rot_fn.bytes)
            ++rotated;
    }
} // namespace

int test_features()
{
    int failures = 0;
    const auto check = [&](bool ok, const char* name)
    { printf("%s: %s\n", ok ? "PASS" : "FAIL", name); failures += !ok; };
    Config settings;
    const Config previous_config = config;
    config = {};
    menu::test_backspace(false);
    check(config.extra.player_text == 0, "player labels default to combined text above the head");
    for (int mode = 1; mode <= 5; ++mode)
    {
        menu::test_backspace(true);
        for (int held = 0; held < 100; ++held)
            menu::test_backspace(true);
        const int expected = mode % 5;
        check(config.extra.player_text == expected && config.esp.agent_name == (expected == 0 || expected == 1 || expected == 3) && config.esp.distance == (expected <= 2), "Backspace advances once per press and wraps all five text modes");
        menu::test_backspace(false);
    }
    config = previous_config;
    config = {};
    menu::test_fov(false, false);
    menu::test_fov(true, false);
    for (int held = 0; held < 100; ++held)
        menu::test_fov(true, false);
    check(config.aimbot.fov == 85, "Up increases aim FOV once per press");
    menu::test_fov(false, false);
    menu::test_fov(false, true);
    check(config.aimbot.fov == 80, "Down decreases aim FOV");
    menu::test_fov(false, false);
    config.aimbot.fov = 799;
    menu::test_fov(true, false);
    check(config.aimbot.fov == 800, "FOV hotkey respects upper limit");
    menu::test_fov(false, false);
    config.aimbot.fov = 2;
    menu::test_fov(false, true);
    check(config.aimbot.fov == 1, "FOV hotkey respects lower limit");
    menu::test_fov(false, false);
    menu::test_fov(true, true);
    check(config.aimbot.fov == 1, "opposite FOV hotkeys together do not change the setting");
    menu::test_fov(false, false);
    config = previous_config;
    check(settings.aimbot.bone == 4 && settings.extra.explosives && !settings.extra.no_recoil && !settings.extra.auto_join && !settings.extra.anti_afk && !settings.extra.feature_hud && !settings.extra.build_x, "requested feature defaults");
    settings.extra.tracers = true;
    settings.extra.tracer_style = 3;
    settings.extra.tracer_color[1] = 0.37f;
    settings.extra.build_x = true;
    settings.radar.items = settings.radar.bags = true;
    settings.radar.helicopters = false;
    const auto encoded = encode_config(settings);
    Config decoded;
    check(decode_config(encoded.c_str(), encoded.size(), decoded) && decoded.extra.build_x && decoded.extra.tracer_style == 3 && decoded.extra.tracer_color[1] == 0.37f, "shared settings round trip with new feature fields");
    check(decoded.esp.minimap_size == 400 && decoded.radar.items && decoded.radar.bags && !decoded.radar.helicopters, "shared settings preserve radar filters and 400 pixel size");
    Config old_settings;
    old_settings.esp.minimap_size = 200;
    old_settings.esp.vehicles = false;
    old_settings.extra.build_x = true;
    unsigned char old_bytes[4 + offsetof(Config, radar)]{3};
    memcpy(old_bytes + 4, &old_settings, offsetof(Config, radar));
    std::string legacy = "XENGINE3:";
    std::uint32_t hash = 2166136261u;
    for (unsigned char byte : old_bytes)
    {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", byte);
        legacy += hex;
        hash = (hash ^ byte) * 16777619u;
    }
    char checksum[9];
    snprintf(checksum, sizeof(checksum), "%08X", hash);
    legacy += checksum;
    check(decode_config(legacy.c_str(), legacy.size(), decoded) && decoded.extra.build_x && decoded.esp.minimap_size == 400 && !decoded.radar.helicopters && !decoded.radar.ground && !decoded.radar.items, "version 3 shared settings migrate radar size and former vehicle visibility");
    using wdgs::actors::Kind;
    check(radar_vehicle_visible(settings, Kind::boat) && radar_vehicle_visible(settings, Kind::buggy) && !radar_vehicle_visible(settings, Kind::heli), "radar vehicle types can be selected independently");
    check(radar_scan_range(settings) == 5000, "automatic radar collection covers its supported range");
    settings.esp.minimap_auto_range = false;
    settings.esp.minimap_range = 600;
    check(radar_scan_range(settings) == 600, "manual radar collection respects selected range");
    settings.esp.minimap = false;
    check(radar_scan_range(settings) == 0, "disabled radar adds no collection range");
    auto corrupted = encoded;
    corrupted[24] = corrupted[24] == '0' ? '1' : '0';
    decoded.aimbot.fov = 234;
    check(!decode_config(corrupted.c_str(), corrupted.size(), decoded) && decoded.aimbot.fov == 234, "corrupt shared settings rejected without replacing current settings");
    check(!decode_config(encoded.c_str(), encoded.size() - 1, decoded) && !decode_config("XENGINE3:", 9, decoded) && !decode_config(nullptr, encoded.size(), decoded), "truncated and missing settings rejected");

    game::ProjectedPlayer player;
    player.bones[BONE_HEAD] = {screen_width * 0.5f + 40, screen_height * 0.5f, true};
    player.bones[BONE_HAND_L] = {screen_width * 0.5f + 2, screen_height * 0.5f, true};
    player.bones[BONE_ROOT] = {screen_width * 0.5f, screen_height * 0.5f, true};
    check(aimbot::target_bone(player, 4) == BONE_HAND_L && aimbot::target_bone(player, 0) == BONE_HEAD, "nearest bone includes limbs, excludes root, preserves fixed bone choices");
    player.bones[BONE_HAND_L].x = std::numeric_limits<float>::quiet_NaN();
    check(aimbot::target_bone(player, 4) == BONE_HEAD, "nearest bone ignores nonfinite projections");
    player.bones[BONE_HEAD].valid = false;
    check(aimbot::target_bone(player, 4) == -1, "missing projected bones cannot become a target");
    CameraIPC camera{};
    camera.fov = 90;
    visual_math::Projection projection(camera);
    const auto center = projection.project({1000, 0, 0});
    check(center.valid && center.x == screen_width * 0.5f && !projection.project({-1000, 0, 0}).valid, "effect projection rejects points behind the camera");

    wdgs::projectile_subsystem::Instance round;
    unsigned char storage[0x320]{};
    round.address = reinterpret_cast<std::uintptr_t>(storage);
    round.owner_internal_index = 10;
    round.location = {100, 0, 0};
    round.velocity = {10000, 0, 0};
    round.flight_time = 0.01;
    check(!wdgs::magic_bullet::test_retarget(round, {100, 1000, 0}, 11, 12), "projectile redirect rejects another player's round");
    check(wdgs::magic_bullet::test_retarget(round, {100, 1000, 0}, 10, 12), "silent projectile redirect accepts local infantry round");
    const auto redirected = read<FVector>(round.address + wdgs::projectile_subsystem::velocity_offset);
    check(redirected.Y == 10000 && redirected.X == 0, "redirect preserves projectile speed");
    round.owner_internal_index = 12;
    check(wdgs::magic_bullet::test_retarget(round, {100, 1000, 0}, 10, 12), "projectile redirect accepts local vehicle round");
    round.landed = true;
    check(!wdgs::magic_bullet::test_retarget(round, {100, 1000, 0}, 10, 12), "landed projectiles are not modified");
    round.landed = false;
    tracers::reset();
    for (unsigned i = 0; i < 1000; ++i)
    {
        round.slot = i;
        round.location = {1000, 0, 0};
        round.flight_time = 0.01;
        tracers::sample(round, 1, 1);
        round.location = {1100, 0, 0};
        round.flight_time = 0.02;
        tracers::sample(round, 1, 1.01);
    }
    game::Snapshot snapshot;
    tracers::snapshot(snapshot, 1.02, 2);
    check(snapshot.trails.size() <= 24 && !snapshot.trails.empty(), "1000 rounds stay within the fixed tracer budget");
    tracers::snapshot(snapshot, 4, 2);
    check(snapshot.trails.empty(), "expired tracers clear without new shots");
    tracers::reset();
    round.slot = 1;
    round.location = {1000, 0, 0};
    round.flight_time = 0.01;
    tracers::sample(round, 1, 1);
    round.location = {1100, 0, 0};
    round.flight_time = 0.02;
    tracers::sample(round, 1, 1.01);
    round.location = {2000, 0, 0};
    tracers::sample(round, 1, 1.09);
    round.location = {2100, 0, 0};
    round.flight_time = 0.03;
    tracers::sample(round, 1, 1.10);
    tracers::snapshot(snapshot, 1.11, 2);
    bool reused_separately = snapshot.trails.size() == 2;
    for (const auto& trail : snapshot.trails)
        reused_separately = reused_separately && trail.count == 2 && trail.points[1].Distance(trail.points[0]) == 100;
    check(reused_separately, "reused projectile slot starts a separate trail");
    tracers::reset();

    Object stats;
    wdgs::weapon_stats::Snapshot weapon;
    weapon.stats_data = stats.addr();
    weapon.stats = stats.addr() + 0x30;
    constexpr std::uintptr_t fields[]{0xB78, 0xCB8, 0xCC0, 0xCC4, 0xCD0, 0xCD4};
    for (auto field : fields)
        write<float>(weapon.stats + field, 2.5f);
    const auto old_ticks = frame_ticks;
    frame_ticks = 1000;
    extras::recoil(weapon, true);
    check(read<float>(weapon.stats + fields[0]) == 0 && read<float>(weapon.stats + fields[5]) == 0, "no recoil changes only verified fields");
    extras::recoil(weapon, false);
    check(read<float>(weapon.stats + fields[0]) == 2.5f && read<float>(weapon.stats + fields[5]) == 2.5f, "disabling no recoil restores original values");
    extras::reset();
    frame_ticks = old_ticks;

    const auto old_base = offsets::base, old_world = offsets::UWorldPtr, old_find = offsets::Functions::StaticFindObject, old_event = offsets::Functions::ProcessEvent;
    Object world, state, instance, local, controller, pawn, ps, mesh, inventory, item, manager, tool, decal, faction;
    std::uintptr_t world_pointer = world.addr();
    offsets::base = 1;
    offsets::UWorldPtr = reinterpret_cast<std::uintptr_t>(&world_pointer) - 1;
    offsets::Functions::StaticFindObject = reinterpret_cast<std::uintptr_t>(&find_object) - 1;
    offsets::Functions::ProcessEvent = reinterpret_cast<std::uintptr_t>(&event) - 1;
    state.put(offsets::UObject::ClassPrivate, session.addr());
    ps.put(offsets::UObject::ClassPrivate, state_type.addr());
    manager.put(offsets::UObject::ClassPrivate, manager_type.addr());
    tool.put(offsets::UObject::ClassPrivate, tool_type.addr());
    decal.put(offsets::UObject::ClassPrivate, decal_type.addr());
    world.put(offsets::World::GameState, state.addr());
    world.put(offsets::World::OwningGameInstance, instance.addr());
    auto local_address = local.addr();
    TArray<std::uintptr_t> locals;
    locals.Data = &local_address;
    locals.Count = locals.Max = 1;
    instance.put(offsets::UGameInstance::LocalPlayers, locals);
    local.put(offsets::UPlayer::PlayerController, controller.addr());
    controller.put(0x2C0, ps.addr());
    controller.put(offsets::APlayerController_Extra::ControllerPawn, pawn.addr());
    ps.put(offsets::APlayerState::PawnPrivate, pawn.addr());
    ps.put(offsets::AWDPlayerStateSession::FactionComponent, faction.addr());
    pawn.put(offsets::ACharacter::Mesh, mesh.addr());
    pawn.put(0x748, inventory.addr());
    inventory.put(0x8C8, item.addr());
    pawn_address = pawn.addr();
    item_address = item.addr();
    manager_address = manager.addr();
    tool_address = tool.addr();
    unsigned char entry[0x90]{};
    const auto decal_address = decal.addr();
    memcpy(entry + 0x18, &decal_address, 8);
    manager.put(0xE8, reinterpret_cast<std::uintptr_t>(entry));
    manager.put(0xF0, 1);
    manager.put(0xF4, 1);
    manager.put(0xF8, 1u);
    manager.put(0x110, 1);
    manager.put(0x114, 128);
    const FVector original{400, 400, 300};
    decal.put(0x230, original);
    decal.put(0x160, 25.0);
    settings = {};
    settings.extra.build_x = true;
    game_actions::test_service(settings, 6000);
    auto position = read<FVector>(decal.addr() + 0x230);
    check(moved == 1 && traces == 1 && std::fabs(position.X - 199) < 0.01 && position.Y == 200, "Silent Build X uses the traced surface, not a fixed point behind the wall");
    settings.extra.build_x = false;
    game_actions::test_service(settings, 6001);
    check(read<FVector>(decal.addr() + 0x230).Distance(original) < 0.01, "Silent Build X restores markers when disabled");
    settings.extra.build_x = true;
    game_actions::test_service(settings, 6002);
    manager.put(0xF8, 0u);
    game_actions::test_service(settings, 6003);
    check(read<FVector>(decal.addr() + 0x230).Distance(original) < 0.01, "Build X restores a surviving decal removed from the manager map");
    manager.put(0xF8, 1u);
    game_actions::test_service(settings, 6004);
    world_pointer = 0;
    const int previous_moves = moved;
    game_actions::test_service(settings, 6005);
    check(moved == previous_moves, "leaving the world discards Build X pointers without touching old objects");
    world_pointer = world.addr();
    settings.extra.build_x = false;
    settings.extra.auto_join = true;
    controller.put(offsets::APlayerController_Extra::ControllerPawn, std::uintptr_t{0});
    game_actions::test_service(settings, 11001);
    game_actions::test_service(settings, 11200);
    check(reserved == 1 && committed == 0, "auto join can reserve on deploy screen and respects its retry interval");
    game_actions::test_service(settings, 11502);
    check(committed == 1, "auto join commits after reservation");
    faction.put(offsets::UWDFactionComponent::FactionTag, FNameValue{777, 0});
    game_actions::test_service(settings, 12003);
    check(reserved == 1 && committed == 1, "auto join stops requesting once already on Manticore");
    settings.extra.auto_join = false;
    settings.extra.anti_afk = true;
    controller.put(offsets::APlayerController_Extra::ControllerPawn, pawn.addr());
    game_actions::test_service(settings, 16002);
    check(rotated == 0, "anti AFK does not turn immediately when enabled");
    game_actions::test_service(settings, 21003);
    check(rotated == 1, "anti AFK acts only after its interval");
    settings = {};
    game_actions::test_service(settings, 21004);

    Object camera_object;
    controller.put(offsets::APlayerController::PlayerCameraManager, camera_object.addr());
    game::Snapshot published;
    published.valid = true;
    published.world = world.addr();
    published.controller = controller.addr();
    published.pawn = pawn.addr();
    published.camera_manager = camera_object.addr();
    published.camera.fov = 90;
    CameraIPC live_camera{};
    live_camera.fov = 40;
    live_camera.rotation.Yaw = 25;
    camera_object.put(offsets::APlayerCameraManager::CameraCachePrivate + offsets::FCameraCacheEntry::POV, live_camera);
    CameraIPC rendered{};
    const int previous_events = event_calls;
    check(game::camera_for_render(published, rendered) && rendered.rotation.Yaw == 25 && rendered.fov == 40 && published.camera.rotation.Yaw == 0, "render camera advances without changing or waiting for entity publication");
    check(event_calls == previous_events, "late camera sample never calls native ProcessEvent");
    const auto before_turn = visual_math::Projection(published.camera).project({1000, 0, 0});
    const auto after_turn = visual_math::Projection(rendered).project({1000, 0, 0});
    check(before_turn.valid && after_turn.valid && after_turn.x < before_turn.x, "fixed world geometry follows a rightward camera turn immediately");
    live_camera.fov = std::numeric_limits<float>::quiet_NaN();
    camera_object.put(offsets::APlayerCameraManager::CameraCachePrivate + offsets::FCameraCacheEntry::POV, live_camera);
    check(game::camera_for_render(published, rendered) && rendered.fov == 90, "invalid camera cache retains the complete published camera");
    controller.put(offsets::APlayerController_Extra::ControllerPawn, std::uintptr_t{0});
    check(!game::camera_for_render(published, rendered), "render rejects an old snapshot after unpossession");
    controller.put(offsets::APlayerController_Extra::ControllerPawn, pawn.addr());
    camera_object.put(offsets::UObject::ObjectFlags, std::uint32_t{0x8000});
    check(!game::camera_for_render(published, rendered), "render rejects a camera pending destruction");
    camera_object.put(offsets::UObject::ObjectFlags, std::uint32_t{0});
    Object grenade, bag, grenade_root, bag_root;
    grenade.put(offsets::UObject::ClassPrivate, explosive_type.addr());
    bag.put(offsets::UObject::ClassPrivate, container_type.addr());
    grenade.put(offsets::AActor::RootComponent, grenade_root.addr());
    bag.put(offsets::AActor::RootComponent, bag_root.addr());
    FTransform transform{};
    transform.Translation = {10000, 0, 0};
    grenade_root.put(offsets::USceneComponent::ComponentToWorld, transform);
    bag_root.put(offsets::USceneComponent::ComponentToWorld, transform);
    settings = {};
    settings.extra.explosives = settings.extra.death_bags = false;
    settings.radar.explosives = settings.radar.bags = true;
    settings.esp.minimap_auto_range = false;
    extras::reset();
    CameraIPC radar_camera{};
    game::Snapshot radar_snapshot;
    const int before_markers = event_calls;
    extras::begin(world.addr(), settings);
    extras::collect(grenade.addr(), radar_camera, settings, radar_snapshot);
    extras::collect(bag.addr(), radar_camera, settings, radar_snapshot);
    extras::finish(radar_camera, settings, radar_snapshot);
    check(radar_snapshot.markers.size() == 2 && !radar_snapshot.markers[0].bag && radar_snapshot.markers[1].bag && !radar_snapshot.markers[0].label[0] && !radar_snapshot.markers[1].label[0] && event_calls == before_markers, "radar-only explosives and bags are collected without label calls or world ESP");
    settings.esp.minimap = false;
    radar_snapshot.markers.clear();
    extras::begin(world.addr(), settings);
    extras::finish(radar_camera, settings, radar_snapshot);
    check(radar_snapshot.markers.empty(), "disabling both marker displays clears radar-only candidates");
    extras::reset();
    world_pointer = 0;
    check(!game::camera_for_render(published, rendered), "render rejects the previous world before another entity scan finishes");
    offsets::base = old_base;
    offsets::UWorldPtr = old_world;
    offsets::Functions::StaticFindObject = old_find;
    offsets::Functions::ProcessEvent = old_event;
    return failures;
}

int test_effect_drawing()
{
    int failures = 0;
    const Config saved = config;
    config = {};
    config.extra.tracers = true;
    config.extra.feature_hud = true;
    config.esp.minimap = false;
    config.aimbot.draw_fov = false;
    game::Snapshot scene;
    scene.valid = true;
    scene.camera.fov = 90;
    scene.time = 1;
    game::Snapshot::Marker grenade;
    grenade.world = {5000, -1500, 800};
    grenade.distance = 52;
    grenade.timed = true;
    grenade.fuse_left = 1.5f;
    grenade.fuse_total = 5;
    strcpy_s(grenade.label, "GRENADE");
    scene.markers.push_back(grenade);
    auto bag = grenade;
    bag.bag = true;
    bag.world.Y = 1500;
    bag.nearby = 2;
    strcpy_s(bag.label, "DEATH BAG");
    scene.markers.push_back(bag);
    game::Snapshot::Trail trail;
    trail.count = 12;
    trail.hue = 0.1f;
    for (int i = 0; i < trail.count; ++i)
        trail.points[i] = {1000.0 + i * 350, -700.0 + i * 70, 100.0 + std::sin(i * 0.3) * 100};
    scene.trails.push_back(trail);
    for (int mode = 0; mode < 5; ++mode)
    {
        config.extra.tracer_style = mode % 4;
        scene.mortar.valid = mode == 4;
        config.extra.mortar_mode = 1;
        scene.mortar.range_m = 600;
        scene.mortar.max_range_m = 1500;
        scene.mortar.sight_mils = 1300;
        bool begun = false;
        for (int attempt = 0; attempt < 100 && !(begun = overlay::begin()); ++attempt)
            Sleep(1);
        bool ok = begun;
        if (begun)
        {
            visuals::draw(scene);
            ok = overlay::end();
        }
        wchar_t path[64];
        swprintf_s(path, L"build/effects-%d.bmp", mode);
        ok = ok && overlay::capture(path);
        printf("%s: effect style %d renders with markers and HUD\n", ok ? "PASS" : "FAIL", mode);
        failures += !ok;
    }
    config = saved;
    return failures;
}
