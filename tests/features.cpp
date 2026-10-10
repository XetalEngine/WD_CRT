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
    Object actor_type, scene_type, trace_type, string_type;
    Object faction_type;
    Object component_fn, eyes_fn, location_fn, trace_fn, reserve_fn, commit_fn, string_fn, library, get_rot_fn, set_rot_fn;
    std::uintptr_t pawn_address, item_address, manager_address, tool_address;
    int moved, traces, reserved, committed, rotated, event_calls;
    int reserve_key, commit_key;
    FNameValue reserved_tag{};
    std::uintptr_t reserved_ps, committed_ps;
    FVector test_eye{100, 200, 300};
    FRotator test_rotation{};
    float hit_fraction = 0.4f;
    DWORD event_thread = 0;
    HWND nested_window = nullptr;
    Config* nested_settings = nullptr;
    int nested_events = -1;
    std::uintptr_t faction_candidates[2]{};
    int faction_candidate_count = 0, faction_scans = 0, faction_frees = 0, virtual_events = 0;

    void __fastcall faction_objects(void*, TArray<std::uintptr_t>* result, unsigned, unsigned, int)
    {
        ++faction_scans;
        result->Count = result->Max = faction_candidate_count;
        if (faction_candidate_count)
        {
            result->Data = static_cast<std::uintptr_t*>(malloc(sizeof(faction_candidates)));
            memcpy(result->Data, faction_candidates, sizeof(faction_candidates));
        }
    }

    void __fastcall free_factions(std::uintptr_t allocation)
    {
        ++faction_frees;
        free(reinterpret_cast<void*>(allocation));
    }

    LRESULT CALLBACK action_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_APP + 42)
            return wparam + lparam;
        return DefWindowProcW(window, message, wparam, lparam);
    }

    void* __fastcall find_object(void*, void* outer, const FString* name, bool)
    {
        const auto s = name->ToWString(200);
        Object* out = nullptr;
        if (s == L"/Script/Engine.Actor")
            out = &actor_type;
        else if (s == L"/Script/Engine.SceneComponent")
            out = &scene_type;
        else if (s == L"/Script/Engine.KismetSystemLibrary")
            out = &trace_type;
        else if (s == L"/Script/Engine.KismetStringLibrary")
            out = &string_type;
        else if (s.find(L"WDGameStateSession") != s.npos)
            out = &session;
        else if (s.find(L"Server_ReserveFaction") != s.npos)
            out = outer == state_type.bytes ? &reserve_fn : nullptr;
        else if (s.find(L"Server_CommitFaction") != s.npos)
            out = s == L"/Script/WDGame.WDPlayerStateSession.Server_CommitFactionReservation" ? &commit_fn : nullptr;
        else if (s.find(L"WDPlayerStateSession") != s.npos)
            out = &state_type;
        else if (s == L"/Script/WDGame.WDFaction")
            out = &faction_type;
        else if (s.find(L"WDPawnBuildableManager") != s.npos)
            out = &manager_type;
        else if (s.find(L"WDItemExtension_BuildBuildable") != s.npos)
            out = &tool_type;
        else if (s.find(L"DecalComponent") != s.npos)
            out = &decal_type;
        else if (s.find(L"GetComponentByClass") != s.npos)
            out = outer == actor_type.bytes ? &component_fn : nullptr;
        else if (s.find(L"GetActorEyesViewPoint") != s.npos)
            out = s == L"/Script/Engine.Actor.GetActorEyesViewPoint" ? &eyes_fn : nullptr;
        else if (s.find(L"K2_SetWorldLocation") != s.npos)
            out = outer == scene_type.bytes ? &location_fn : nullptr;
        else if (s.find(L"LineTraceSingle") != s.npos)
            out = s == L"/Script/Engine.KismetSystemLibrary:LineTraceSingle" ? &trace_fn : nullptr;
        else if (s.find(L"Conv_StringToName") != s.npos)
            out = s == L"/Script/Engine.KismetStringLibrary.Conv_StringToName" ? &string_fn : nullptr;
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
        event_thread = GetCurrentThreadId();
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
            memcpy(bytes, &test_eye, sizeof(test_eye));
            memcpy(bytes + sizeof(test_eye), &test_rotation, sizeof(test_rotation));
        }
        else if (function == trace_fn.bytes)
        {
            memcpy(bytes + 0x5C, &hit_fraction, 4);
            bytes[0x180] = hit_fraction >= 0;
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
        {
            ++reserved;
            memcpy(&reserved_tag, bytes, sizeof(reserved_tag));
            memcpy(&reserve_key, bytes + 8, sizeof(reserve_key));
            reserved_ps = obj;
            if (nested_window)
            {
                const auto window = nested_window;
                nested_window = nullptr;
                const int before = event_calls;
                frame_ticks += 500;
                game_actions::update(*nested_settings, true);
                MSG message{};
                while (PeekMessageA(&message, window, WM_APP, 0xBFFF, PM_REMOVE))
                    DispatchMessageA(&message);
                nested_events = event_calls - before;
            }
        }
        else if (function == commit_fn.bytes)
        {
            ++committed;
            memcpy(&commit_key, bytes, sizeof(commit_key));
            committed_ps = obj;
        }
        else if (function == get_rot_fn.bytes)
        {
            const FRotator rotation{0, 20, 0};
            memcpy(bytes, &rotation, sizeof(rotation));
        }
        else if (function == set_rot_fn.bytes)
            ++rotated;
    }

    void __fastcall virtual_event(void* object, void* function, void* params)
    {
        ++virtual_events;
        event(object, function, params);
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
    for (int mode = 1; mode <= 4; ++mode)
    {
        menu::test_backspace(true);
        for (int held = 0; held < 100; ++held)
            menu::test_backspace(true);
        const int expected = mode == 4 ? 0 : mode + 1;
        check(config.extra.player_text == expected && config.esp.agent_name == (expected == 0 || expected == 3) && config.esp.distance == (expected == 0 || expected == 2), "Backspace advances once per press and wraps all four text modes");
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
    check(settings.aimbot.bone == 4 && settings.extra.explosives && settings.magic_ignore_visibility && !settings.extra.no_recoil && !settings.extra.auto_join && !settings.extra.anti_afk && !settings.extra.feature_hud && !settings.extra.build_x, "requested feature defaults");
    settings.extra.tracers = true;
    settings.extra.tracer_style = 3;
    settings.extra.tracer_color[1] = 0.37f;
    settings.extra.build_x = true;
    settings.radar.items = settings.radar.bags = true;
    settings.radar.helicopters = false;
    settings.selected_visible_color[0] = 0.45f;
    settings.magic_ignore_visibility = false;
    settings.magic_min_distance = 250;
    const auto encoded = encode_config(settings);
    Config decoded;
    check(decode_config(encoded.c_str(), encoded.size(), decoded) && decoded.extra.build_x && decoded.extra.tracer_style == 3 && decoded.extra.tracer_color[1] == 0.37f, "shared settings round trip with new feature fields");
    check(decoded.esp.minimap_size == 400 && decoded.radar.items && decoded.radar.bags && !decoded.radar.helicopters, "shared settings preserve radar filters and 400 pixel size");
    check(decoded.selected_visible_color[0] == 0.45f && decoded.selected_visible_color[1] == 1 && decoded.selected_visible_color[2] == 1, "shared settings preserve the visible target color");
    check(!decoded.magic_ignore_visibility, "shared settings preserve a disabled Magic visibility bypass");
    check(encoded.compare(0, 9, "XENGINE7:") == 0 && decoded.magic_min_distance == 250, "version 7 sharing preserves Magic minimum distance");
    Config old_settings;
    old_settings.esp.minimap_size = 200;
    old_settings.esp.vehicles = false;
    old_settings.extra.build_x = true;
    old_settings.extra.player_text = 1;
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
    check(decode_config(legacy.c_str(), legacy.size(), decoded) && decoded.extra.build_x && decoded.extra.player_text == 0 && decoded.esp.minimap_size == 400 && !decoded.radar.helicopters && !decoded.radar.ground && !decoded.radar.items, "version 3 shared settings migrate radar options and retired feet labels");
    old_settings.colors.selected[2] = 0.4f;
    old_settings.radar.bags = true;
    unsigned char v4_bytes[4 + offsetof(Config, selected_visible_color)]{4};
    memcpy(v4_bytes + 4, &old_settings, offsetof(Config, selected_visible_color));
    legacy = "XENGINE4:";
    hash = 2166136261u;
    for (unsigned char byte : v4_bytes)
    {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", byte);
        legacy += hex;
        hash = (hash ^ byte) * 16777619u;
    }
    snprintf(checksum, sizeof(checksum), "%08X", hash);
    legacy += checksum;
    check(decode_config(legacy.c_str(), legacy.size(), decoded) && decoded.colors.selected[2] == 0.4f && decoded.radar.bags && decoded.esp.minimap_size == 200 && decoded.selected_visible_color[0] == 0 && decoded.selected_visible_color[1] == 1 && decoded.selected_visible_color[2] == 1, "version 4 shared settings keep custom colors and radar and add cyan visible targets");
    old_settings.selected_visible_color[0] = 0.2f;
    unsigned char v5_bytes[4 + offsetof(Config, magic_ignore_visibility)]{5};
    memcpy(v5_bytes + 4, &old_settings, offsetof(Config, magic_ignore_visibility));
    legacy = "XENGINE5:";
    hash = 2166136261u;
    for (unsigned char byte : v5_bytes)
    {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", byte);
        legacy += hex;
        hash = (hash ^ byte) * 16777619u;
    }
    snprintf(checksum, sizeof(checksum), "%08X", hash);
    legacy += checksum;
    check(decode_config(legacy.c_str(), legacy.size(), decoded) && decoded.magic_ignore_visibility && decoded.selected_visible_color[0] == 0.2f && decoded.radar.bags, "version 5 shared settings enable Magic visibility bypass and preserve prior settings");
    old_settings.magic_ignore_visibility = false;
    old_settings.magic_min_distance = 500;
    unsigned char v6_bytes[4 + offsetof(Config, magic_min_distance)]{6};
    memcpy(v6_bytes + 4, &old_settings, offsetof(Config, magic_min_distance));
    legacy = "XENGINE6:";
    hash = 2166136261u;
    for (unsigned char byte : v6_bytes)
    {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", byte);
        legacy += hex;
        hash = (hash ^ byte) * 16777619u;
    }
    snprintf(checksum, sizeof(checksum), "%08X", hash);
    legacy += checksum;
    check(decode_config(legacy.c_str(), legacy.size(), decoded) && decoded.magic_min_distance == 0 && !decoded.magic_ignore_visibility && decoded.selected_visible_color[0] == 0.2f && decoded.radar.bags, "version 6 sharing defaults Magic minimum to zero and preserves prior choices");
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
    round.location = {100, 0, 1000};
    round.velocity = {0, 0, 10000};
    round.flight_time = 1.2;
    check(wdgs::magic_bullet::test_retarget(round, {100, 0, 100}, 10, 12), "upward projectile accepts a target below it");
    const auto downward = read<FVector>(round.address + wdgs::projectile_subsystem::velocity_offset);
    check(downward.X == 0 && downward.Y == 0 && downward.Z == -10000, "upward projectile reverses downward while preserving speed");
    round.external_movement = 1;
    check(!wdgs::magic_bullet::test_retarget(round, {100, 0, 100}, 10, 12), "externally controlled projectiles are not modified");
    round.external_movement = 0;
    round.flight_time = 0;
    check(!wdgs::magic_bullet::test_retarget(round, {100, 0, 100}, 10, 12), "projectiles without flight time are not modified");
    {
        using namespace wdgs::projectile_subsystem;
        Object controller, subsystem, projectile;
        subsystem.put(pool_offset, projectile.addr());
        subsystem.put(allocated_offset, std::uint32_t(1));
        subsystem.put(inline_bits_offset, std::uint32_t(1));
        projectile.put(location_offset, FVector{0, 0, 100});
        projectile.put(owner_internal_index_offset, std::uint32_t(10));
        projectile.put(flight_time_offset, 1.0);
        const double saved_time = frame_time;
        frame_time = 10;
        std::vector<game::ProjectedPlayer> targets(1);
        auto& target = targets[0];
        target.actor_addr = controller.addr();
        target.player.has_bones = target.player.isVisible = true;
        target.player.health = 100;
        target.bones[BONE_HEAD] = {screen_width * 0.5f, screen_height * 0.5f, true};
        AimbotSettings aim;
        aim.bone = 0;
        aim.silent_aim = aim.magic_bullet = true;
        PredictionSettings prediction;
        prediction.enabled = true;
        prediction.bullet_drop = prediction.show_line = false;
        target.player.velocity = {0, 1000, 0};
        game::PredictionLine line;
        float minimum = 100;
        bool ignore_visibility = true, menu_visible = false;
        const auto fresh_round = [&]()
        {
            wdgs::magic_bullet::reset();
            test_subsystem(subsystem.addr());
            projectile.put(velocity_offset, FVector{10000, 0, 0});
        };
        const auto tick = [&](float distance, FVector bone)
        {
            target.player.distance = distance;
            target.player.bones[BONE_HEAD] = bone;
            aimbot::tick(targets, controller.bytes, camera, aim, prediction, 10000, 0, 0, line, {}, false, {}, 10, 12, menu_visible, ignore_visibility, minimum);
            return read<FVector>(projectile.addr() + velocity_offset);
        };
        aimbot::reset();
        aimbot::test_key(true);
        fresh_round();
        auto velocity = tick(99, {10000, 0, 100});
        check(velocity.Y > 0, "combined modes use Silent prediction below Magic minimum");
        const auto once = velocity;
        velocity = tick(99, {0, 10000, 100});
        check(velocity.X == once.X && velocity.Y == once.Y, "nearby Silent redirects the same round only once");
        velocity = tick(100, {0, 10000, 100});
        check(velocity.Y > 9999 && std::fabs(velocity.X) < 0.001, "Magic starts exactly at the minimum and takes over an existing Silent round");
        velocity = tick(101, {10000, 0, 100});
        check(velocity.X > 9999 && std::fabs(velocity.Y) < 0.001, "Magic keeps steering above minimum using the raw bone without prediction");
        velocity = tick(99, {0, 10000, 100});
        check(velocity.X > 9999 && std::fabs(velocity.Y) < 0.001, "returning below minimum stops continuous steering without reapplying Silent");
        target.player.isVisible = false;
        fresh_round();
        velocity = tick(99, {0, 10000, 100});
        check(velocity.X == 10000 && velocity.Y == 0, "Magic visibility bypass does not leak into nearby Silent mode");
        velocity = tick(100, {0, 10000, 100});
        check(velocity.Y > 9999, "Magic visibility bypass still works at the minimum");
        ignore_visibility = false;
        fresh_round();
        velocity = tick(100, {0, 10000, 100});
        check(velocity.X == 10000 && velocity.Y == 0, "disabled Magic bypass still blocks hidden targets above minimum");
        ignore_visibility = target.player.isVisible = true;
        aim.silent_aim = false;
        fresh_round();
        tick(1, {0, 10000, 100});
        velocity = tick(1, {0, -10000, 100});
        check(velocity.Y < -9999, "Magic alone ignores the combined-mode minimum");
        aim.silent_aim = true;
        aim.magic_bullet = false;
        fresh_round();
        tick(500, {0, 10000, 100});
        velocity = tick(500, {0, -10000, 100});
        check(velocity.Y > 9999, "Silent alone remains once per round beyond the minimum");
        aim.magic_bullet = true;
        minimum = 0;
        fresh_round();
        tick(1, {0, 10000, 100});
        velocity = tick(1, {0, -10000, 100});
        check(velocity.Y < -9999, "zero minimum preserves continuous Magic at close range");
        target.is_vehicle = target.team_known = true;
        minimum = 100;
        fresh_round();
        tick(99, {0, 10000, 100});
        velocity = tick(100, {0, -10000, 100});
        check(velocity.Y < -9999, "vehicle targets use the same distance threshold");
        menu_visible = true;
        fresh_round();
        velocity = tick(100, {0, 10000, 100});
        check(velocity.X == 10000 && velocity.Y == 0, "opening the menu still pauses projectile targeting");
        aimbot::test_key(false);
        aimbot::reset();
        wdgs::magic_bullet::reset();
        frame_time = saved_time;
    }
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
    const auto old_names = offsets::GNames, old_objects = offsets::Functions::GetObjectsOfClass, old_free = offsets::Functions::FreeObjectName;
    Object world, state, instance, local, controller, pawn, ps, mesh, inventory, item, manager, tool, decal, faction;
    std::uintptr_t world_pointer = world.addr();
    offsets::base = 1;
    offsets::UWorldPtr = reinterpret_cast<std::uintptr_t>(&world_pointer) - 1;
    offsets::Functions::StaticFindObject = reinterpret_cast<std::uintptr_t>(&find_object) - 1;
    offsets::Functions::ProcessEvent = reinterpret_cast<std::uintptr_t>(&event) - 1;
    offsets::Functions::GetObjectsOfClass = reinterpret_cast<std::uintptr_t>(&faction_objects) - 1;
    offsets::Functions::FreeObjectName = reinterpret_cast<std::uintptr_t>(&free_factions) - 1;
    std::uintptr_t state_vtable[0x4D]{};
    state_vtable[0x4C] = reinterpret_cast<std::uintptr_t>(&virtual_event);
    ps.put(offsets::UObject::VTable, reinterpret_cast<std::uintptr_t>(state_vtable));
    alignas(8) unsigned char names[2048]{};
    std::uintptr_t name_pool[3]{0, 0, reinterpret_cast<std::uintptr_t>(names)};
    offsets::GNames = reinterpret_cast<std::uintptr_t>(name_pool) - 1;
    const auto name_entry = [&](unsigned index, const char* text)
    {
        const auto length = strlen(text);
        const auto header = static_cast<std::uint16_t>(length << 6);
        memcpy(names + index * 8 + 8, &header, sizeof(header));
        memcpy(names + index * 8 + 12, text, length);
    };
    name_entry(128, "Meta.Alignment.Faction.Charlie");
    name_entry(144, "Meta.Alignment.Faction.Bravo");
    name_entry(160, "Meta_Alignment_Faction_Charlie");
    name_entry(176, "DA_Faction_Manticore");
    Object faction_actor, faction_data, named_actor, named_data;
    faction_actor.put(offsets::UObject::ClassPrivate, faction_type.addr());
    faction_actor.put(0x2C8, faction_data.addr());
    faction_data.put(0x30, FNameValue{128, 0});
    faction_data.put(offsets::UObject::NamePrivate, FNameValue{160, 0});
    named_actor.put(offsets::UObject::ClassPrivate, faction_type.addr());
    named_actor.put(0x2C8, named_data.addr());
    named_data.put(0x30, FNameValue{144, 7});
    named_data.put(offsets::UObject::NamePrivate, FNameValue{176, 0});
    faction_candidates[0] = faction_actor.addr();
    faction_candidates[1] = named_actor.addr();
    faction_candidate_count = 1;
    state.put(offsets::UObject::ClassPrivate, session.addr());
    ps.put(offsets::UObject::ClassPrivate, state_type.addr());
    manager.put(offsets::UObject::ClassPrivate, manager_type.addr());
    tool.put(offsets::UObject::ClassPrivate, tool_type.addr());
    decal.put(offsets::UObject::ClassPrivate, decal_type.addr());
    component_fn.put(offsets::UObject::OuterPrivate, actor_type.addr());
    eyes_fn.put(offsets::UObject::OuterPrivate, actor_type.addr());
    location_fn.put(offsets::UObject::OuterPrivate, scene_type.addr());
    trace_fn.put(offsets::UObject::OuterPrivate, actor_type.addr());
    reserve_fn.put(offsets::UObject::OuterPrivate, state_type.addr());
    commit_fn.put(offsets::UObject::OuterPrivate, state_type.addr());
    string_fn.put(offsets::UObject::OuterPrivate, string_type.addr());
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
    game_actions::test_service(settings, 1);
    check(moved == 0 && traces == 0, "Build X rejects a resolved function belonging to the wrong class");
    trace_fn.put(offsets::UObject::OuterPrivate, trace_type.addr());
    game_actions::test_service(settings, 6000);
    auto position = read<FVector>(decal.addr() + 0x230);
    check(moved == 1 && traces == 1 && std::fabs(position.X - 199) < 0.01 && position.Y == 200, "Build X resolves owned and alternate paths and moves the X to the traced hammer surface");
    test_rotation.Yaw = 90;
    hit_fraction = 0.6f;
    game_actions::test_service(settings, 6000);
    position = read<FVector>(decal.addr() + 0x230);
    check(std::fabs(position.X - 100) < 0.01 && std::fabs(position.Y - 349) < 0.01, "Build X follows a new aim direction and surface distance");
    test_rotation = {};
    hit_fraction = 0.4f;
    settings.extra.build_x = false;
    game_actions::test_service(settings, 6001);
    check(read<FVector>(decal.addr() + 0x230).Distance(original) < 0.01, "Silent Build X restores markers when disabled");
    settings.extra.build_x = true;
    game_actions::test_service(settings, 6002);
    const auto consumed = read<FVector>(decal.addr() + 0x230);
    const int before_consumed = moved;
    manager.put(0xF8, 0u);
    game_actions::test_service(settings, 6003);
    check(moved == before_consumed && read<FVector>(decal.addr() + 0x230).Distance(consumed) < 0.01, "Build X forgets a consumed map entry without moving it again, matching SPOT");
    decal.put(0x230, original);
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
    state.put(offsets::UObject::ObjectFlags, std::uint32_t{0x8000});
    game_actions::test_service(settings, 11000);
    check(reserved == 0 && committed == 0 && strcmp(game_actions::faction_status().result, "no live match") == 0, "auto faction does not send RPCs in an invalid match context");
    state.put(offsets::UObject::ObjectFlags, std::uint32_t{0});
    game_actions::test_service(settings, 11001);
    game_actions::test_service(settings, 11200);
    check(reserved == 1 && committed == 0, "auto join can reserve on deploy screen and respects its retry interval");
    check(reserved_tag.ComparisonIndex == 128 && reserved_tag.Number == 0 && reserve_key > 0 && reserved_ps == ps.addr(), "auto faction sends the live Charlie tag rather than a fabricated string-conversion result");
    check(strcmp(game_actions::faction_status().result, "reserve sent; awaiting commit") == 0, "auto faction distinguishes reserve dispatch from commit dispatch");
    game_actions::test_service(settings, 11502);
    check(committed == 1 && commit_key == reserve_key && committed_ps == reserved_ps, "auto join commits the same reservation key on the same player state");
    check(strcmp(game_actions::faction_status().result, "commit sent; waiting for faction") == 0, "auto faction exposes completed commit dispatch while awaiting replication");
    faction.put(offsets::UWDFactionComponent::FactionTag, FNameValue{128, 0});
    game_actions::test_service(settings, 12003);
    check(reserved == 1 && committed == 1, "auto join stops requesting once already on Manticore");
    check(strcmp(game_actions::faction_status().result, "joined Manticore") == 0, "auto faction reports success only after the faction value changes");
    faction.put(offsets::UWDFactionComponent::FactionTag, FNameValue{});
    game_actions::test_service(settings, 12504);
    const auto canceled_key = reserve_key;
    settings.extra.auto_join = false;
    game_actions::test_service(settings, 12505);
    settings.extra.auto_join = true;
    game_actions::test_service(settings, 12506);
    check(reserved == 3 && committed == 1 && reserve_key != canceled_key, "disabling the only action cancels its pending faction reservation before re-enabling");
    game_actions::test_service(settings, 13007);
    check(committed == 2 && commit_key == reserve_key, "re-enabled auto faction commits the new reservation");
    game_actions::test_service(settings, 13508);
    Object replacement_ps;
    replacement_ps.put(offsets::UObject::VTable, reinterpret_cast<std::uintptr_t>(state_vtable));
    replacement_ps.put(offsets::UObject::ClassPrivate, state_type.addr());
    replacement_ps.put(offsets::AWDPlayerStateSession::FactionComponent, faction.addr());
    controller.put(offsets::APlayerController_Extra::ControllerPlayerState, replacement_ps.addr());
    game_actions::test_service(settings, 13509);
    check(reserved == 5 && committed == 2 && reserved_ps == replacement_ps.addr(), "replaced player state starts a fresh reservation instead of committing the previous player's key");
    controller.put(offsets::APlayerController_Extra::ControllerPlayerState, ps.addr());
    check(faction_scans == 1 && faction_frees == 1, "live faction discovery is cached and releases its engine list");
    check(virtual_events == reserved + committed, "all faction RPCs use the player state's virtual ProcessEvent");
    Object next_world;
    next_world.put(offsets::World::GameState, state.addr());
    next_world.put(offsets::World::OwningGameInstance, instance.addr());
    world_pointer = next_world.addr();
    faction_candidate_count = 0;
    const int before_missing = reserved;
    game_actions::test_service(settings, 14000);
    check(faction_scans == 2 && reserved == before_missing && strcmp(game_actions::faction_status().result, "Manticore tag unavailable") == 0, "a new world discards the previous faction tag and waits for live data");
    faction_candidate_count = 2;
    game_actions::test_service(settings, 14500);
    check(faction_scans == 2 && reserved == before_missing, "missing faction discovery retries no sooner than two seconds");
    game_actions::test_service(settings, 16000);
    check(faction_scans == 3 && faction_frees == 2 && reserved == before_missing + 1 && reserved_tag.ComparisonIndex == 144 && reserved_tag.Number == 7, "live Manticore asset wins over Charlie and preserves both FName fields");
    state_vtable[0x4C] = 0;
    const int before_invalid_call = virtual_events;
    game_actions::test_service(settings, 16500);
    check(virtual_events == before_invalid_call && strcmp(game_actions::faction_status().result, "commit call failed") == 0, "an unavailable virtual ProcessEvent fails without calling through a null slot");
    state_vtable[0x4C] = reinterpret_cast<std::uintptr_t>(&virtual_event);
    world_pointer = world.addr();
    faction_candidate_count = 1;
    settings.extra.auto_join = false;
    settings.extra.anti_afk = true;
    controller.put(offsets::APlayerController_Extra::ControllerPawn, pawn.addr());
    game_actions::test_service(settings, 17002);
    check(rotated == 0, "anti AFK does not turn immediately when enabled");
    game_actions::test_service(settings, 22003);
    check(rotated == 1, "anti AFK acts only after its interval");
    settings = {};
    game_actions::test_service(settings, 22004);

    // Exercise the actual Windows callback, not just its service function.
    WNDCLASSW action_class{};
    action_class.lpfnWndProc = action_proc;
    action_class.hInstance = GetModuleHandleW(nullptr);
    action_class.lpszClassName = L"UnrealWindow";
    const ATOM action_atom = RegisterClassW(&action_class);
    const HWND action_window = action_atom ? CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, action_class.lpszClassName, L"Build X callback test", WS_POPUP | WS_VISIBLE, -100, -100, 1, 1, nullptr, nullptr, action_class.hInstance, nullptr) : nullptr;
    check(action_window != nullptr, "create disposable Unreal window for action callback");
    if (action_window)
    {
        const auto pump = []()
        {
            MSG message{};
            for (int i = 0; i < 128 && PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE); ++i)
                DispatchMessageA(&message);
        };
        const auto publish = [&](const Config& options, bool menu)
        {
            struct Input
            {
                const Config& options;
                bool menu;
            } input{options, menu};
            const auto worker = CreateThread(nullptr, 0, [](void* value) -> DWORD
                                             {
                const auto& input = *static_cast<Input*>(value);
                game_actions::update(input.options, input.menu);
                return 0; }, &input, 0, nullptr);
            if (!worker || WaitForSingleObject(worker, 5000) != WAIT_OBJECT_0)
            {
                printf("FAIL: worker publication stalled\n");
                ExitProcess(1);
            }
            CloseHandle(worker);
        };
        decal.put(0x230, original);
        settings.extra.build_x = true;
        settings.aimbot.enabled = false;
        frame_ticks = 30000;
        const int before_callback = moved;
        publish(settings, false);
        check(moved == before_callback, "Build X publication waits for the window message callback");
        check(strcmp(game_actions::build_status().delivery, "waiting for callback") == 0, "menu status exposes an undelivered Build X callback");
        MSG wake{};
        check(SendMessageW(action_window, WM_APP + 42, 17, 25) == 42, "window callback forwards unrelated messages and their return values");
        PostMessageW(action_window, WM_APP + 42, 0, 0);
        while (PeekMessageA(&wake, action_window, WM_APP + 42, WM_APP + 42, PM_REMOVE))
            DispatchMessageA(&wake);
        check(moved == before_callback, "ordinary window messages do not run queued game actions");
        while (PeekMessageA(&wake, action_window, WM_APP, 0xBFFF, PM_REMOVE))
            DispatchMessageA(&wake);
        position = read<FVector>(decal.addr() + 0x230);
        check(moved == before_callback + 1 && std::fabs(position.X - 199) < 0.01 && position.Y == 200, "window-filtered message pump receives the action wake and moves the marker");
        check(event_thread == GetCurrentThreadId(), "worker publication executes native calls on the window owner thread");
        const auto delivered = game_actions::build_status();
        check(strcmp(delivered.delivery, "callback received") == 0 && strcmp(delivered.result, "X follows hammer aim") == 0, "menu status records real callback delivery and its active result");
        test_rotation.Yaw = 90;
        hit_fraction = 0.6f;
        frame_ticks += 16;
        publish(settings, false);
        pump();
        position = read<FVector>(decal.addr() + 0x230);
        check(std::fabs(position.X - 100) < 0.01 && std::fabs(position.Y - 349) < 0.01, "message callback follows a changed hammer aim point");
        frame_ticks += 16;
        publish(settings, true);
        pump();
        check(read<FVector>(decal.addr() + 0x230).Distance(original) < 0.01 && strcmp(game_actions::build_status().result, "X follows hammer aim") == 0, "opening the menu restores the X but preserves its last active diagnostic");
        frame_ticks += 16;
        publish(settings, false);
        pump();
        settings.extra.build_x = false;
        frame_ticks += 16;
        publish(settings, false);
        pump();
        check(read<FVector>(decal.addr() + 0x230).Distance(original) < 0.01, "disabling Build X restores its marker on the game callback");
        settings = {};
        settings.extra.auto_join = true;
        settings.aimbot.enabled = false;
        controller.put(offsets::APlayerController_Extra::ControllerPawn, std::uintptr_t{0});
        frame_ticks = 40000;
        const int before_reserve = reserved, before_commit = committed;
        publish(settings, true);
        check(reserved == before_reserve && strcmp(game_actions::faction_status().delivery, "waiting for callback") == 0, "auto faction publishes while the menu is open and waits for its game callback");
        while (PeekMessageA(&wake, action_window, WM_APP, 0xBFFF, PM_REMOVE))
            DispatchMessageA(&wake);
        check(reserved == before_reserve + 1 && strcmp(game_actions::faction_status().delivery, "callback received") == 0, "window-filtered callback reserves a faction on the deploy screen without a pawn");
        frame_ticks = 40200;
        publish(settings, true);
        pump();
        check(reserved == before_reserve + 1 && committed == before_commit, "faction callback keeps the 500 ms RPC interval");
        frame_ticks = 40500;
        publish(settings, true);
        pump();
        check(committed == before_commit + 1 && commit_key == reserve_key, "faction callback commits the reserved key");
        faction.put(offsets::UWDFactionComponent::FactionTag, FNameValue{128, 0});
        frame_ticks = 41000;
        publish(settings, true);
        pump();
        check(reserved == before_reserve + 1 && committed == before_commit + 1 && strcmp(game_actions::faction_status().result, "joined Manticore") == 0, "faction callback stops requests after the replicated faction becomes Manticore");
        faction.put(offsets::UWDFactionComponent::FactionTag, FNameValue{});
        frame_ticks = 41500;
        nested_window = action_window;
        nested_settings = &settings;
        publish(settings, true);
        const bool got_wake = PeekMessageA(&wake, action_window, WM_APP, 0xBFFF, PM_REMOVE) != 0;
        if (got_wake)
            DispatchMessageA(&wake);
        check(got_wake && nested_events == 0 && committed == before_commit + 1, "ProcessEvent message pumping cannot run actions recursively");
        pump();
        check(committed == before_commit + 2 && commit_key == reserve_key, "a wake consumed by nested dispatch is delivered after the outer callback returns");
        frame_ticks = 42500;
        publish(settings, true);
        frame_ticks = 43500;
        publish(settings, true);
        check(strcmp(game_actions::faction_status().delivery, "callback not delivered") == 0, "an unpumped faction request gets an explicit delivery diagnostic");
        pump();
        check(strcmp(game_actions::faction_status().delivery, "callback received") == 0, "callback delivery recovers after the window starts pumping again");
        controller.put(offsets::APlayerController_Extra::ControllerPawn, pawn.addr());
        settings.extra.build_x = true;
        frame_ticks += 16;
        publish(settings, false);
        pump();
        game_actions::stop();
        while (PeekMessageA(&wake, action_window, WM_APP, 0xBFFF, PM_REMOVE))
            DispatchMessageA(&wake);
        check(read<FVector>(decal.addr() + 0x230).Distance(original) < 0.01, "callback cleanup restores the X before detaching");
        check(reinterpret_cast<WNDPROC>(GetWindowLongPtrW(action_window, GWLP_WNDPROC)) == action_proc && SendMessageW(action_window, WM_APP + 42, 20, 22) == 42, "shutdown restores the original game window procedure");
        const int after_stop = event_calls;
        frame_ticks += 5000;
        publish(settings, false);
        pump();
        check(event_calls == after_stop && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(action_window, GWLP_WNDPROC)) == action_proc, "updates after shutdown cannot reinstall or run game actions");
        DestroyWindow(action_window);
    }
    if (action_atom)
        UnregisterClassW(action_class.lpszClassName, action_class.hInstance);
    test_rotation = {};
    hit_fraction = 0.4f;
    frame_ticks = old_ticks;
    settings = {};

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
    offsets::GNames = old_names;
    offsets::Functions::GetObjectsOfClass = old_objects;
    offsets::Functions::FreeObjectName = old_free;
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
    strcpy_s(bag.label, "d-bag");
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
