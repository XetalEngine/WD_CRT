#include "../src/stdafx.h"
#include "../src/config.h"
#include "../src/menu.h"
#include "../src/overlay.h"
#include "../src/prediction.h"
#include "../src/visuals.h"
#include <cmath>

int test_frame_exchange();
int test_name_conversion();
int test_transitions();
int test_features();
int test_effect_drawing();
int benchmark_crowd(bool effects = false, bool camera_pan = false);

namespace
{
    int failures;
    int local_memory_faults;
    std::uintptr_t test_base;
    std::uintptr_t test_end;

    LONG CALLBACK count_memory_faults(EXCEPTION_POINTERS* exception)
    {
        const auto address = reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress);
        if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && address >= test_base && address < test_end)
            ++local_memory_faults;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    LRESULT CALLBACK child_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_DESTROY)
            PostQuitMessage(0);
        return DefWindowProcW(window, message, wparam, lparam);
    }

    void check(bool passed, const char* name)
    {
        printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
        if (!passed)
            ++failures;
    }

    bool begin_frame()
    {
        const auto start = GetTickCount64();
        do
        {
            if (overlay::begin())
                return true;
            if (!overlay::window())
                return false;
            Sleep(1);
        } while (GetTickCount64() - start < 2000);
        return false;
    }

    bool pixel(const wchar_t* file, int x, int y, int red, int green, int blue, int alpha)
    {
        FILE* input = nullptr;
        if (_wfopen_s(&input, file, L"rb"))
            return false;
        BITMAPFILEHEADER header;
        BITMAPINFOHEADER info;
        fread(&header, sizeof(header), 1, input);
        fread(&info, sizeof(info), 1, input);
        fseek(input, header.bfOffBits + (y * info.biWidth + x) * 4, SEEK_SET);
        unsigned char color[4]{};
        fread(color, 4, 1, input);
        fclose(input);
        return std::abs(color[0] - blue) <= 2 && std::abs(color[1] - green) <= 2 && std::abs(color[2] - red) <= 2 && std::abs(color[3] - alpha) <= 2;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--crowd-bench") == 0)
        return benchmark_crowd();
    if (argc == 2 && strcmp(argv[1], "--effects-bench") == 0)
        return benchmark_crowd(true);
    if (argc == 2 && strcmp(argv[1], "--camera-bench") == 0)
        return benchmark_crowd(false, true);
    if (argc == 3 && strcmp(argv[1], "--overlay-host") == 0)
    {
        wchar_t title[64];
        swprintf_s(title, L"WD Rewrite Test %d", atoi(argv[2]));
        WNDCLASSW wc{};
        wc.lpfnWndProc = child_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"WDRewriteChild";
        RegisterClassW(&wc);
        HWND window = CreateWindowExW(0, wc.lpszClassName, title, WS_POPUP, 0, 0, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
        if (!window)
            return 2;
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
            DispatchMessageW(&message);
        return 0;
    }
    CreateDirectoryW(L"build", nullptr);
    failures += test_frame_exchange();
    failures += test_name_conversion();
    failures += test_transitions();
    failures += test_features();
    test_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(test_base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(test_base + dos->e_lfanew);
    test_end = test_base + nt->OptionalHeader.SizeOfImage;
    void* fault_counter = AddVectoredExceptionHandler(1, count_memory_faults);
    check(fault_counter != nullptr, "observe memory validation faults");
    if (!fault_counter)
        return 1;
    auto* region = static_cast<unsigned char*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(region != nullptr, "test allocation");
    if (!region)
        return 1;
    DWORD old;
    VirtualProtect(region + 4096, 4096, PAGE_NOACCESS, &old);
    region[4095] = 0x6A;
    unsigned char copied[2]{1, 1};
    check(read<unsigned char>(reinterpret_cast<std::uintptr_t>(region + 4095)) == 0x6A, "read at page boundary");
    check(!read_mem(reinterpret_cast<std::uintptr_t>(region + 4095), copied, 2) && copied[0] == 0 && copied[1] == 0, "cross-page read fails and clears output");
    check(!is_valid_ptr(region + 4095, 2), "cross-page pointer validation");
    check(!read_mem(reinterpret_cast<std::uintptr_t>(region), region + 4096, 1), "unwritable read output rejected");
    check(!write<int>(reinterpret_cast<std::uintptr_t>(region + 4096), 42), "protected write rejected");
    check(!write_mem(reinterpret_cast<std::uintptr_t>(region), region + 4096, 1), "unreadable write input rejected");
    check(!write_mem(reinterpret_cast<std::uintptr_t>(region + 4095), copied, 2) && region[4095] == 0x6A, "cross-page write rejected before modifying data");
    check(!is_userland_range(UINTPTR_MAX - 8, 20) && !is_valid_ptr(nullptr), "invalid pointer ranges");
    check(!is_valid_ptr(reinterpret_cast<void*>(0x500000005)), "reject invalid pointer recorded in crash dump");
    RemoveVectoredExceptionHandler(fault_counter);
    check(local_memory_faults == 0, "memory checks do not depend on local exception handlers");
    VirtualFree(region, 0, MEM_RELEASE);

    Config invalid;
    invalid.aimbot.fov = std::numeric_limits<float>::quiet_NaN();
    invalid.aimbot.bone = 400;
    invalid.esp.minimap_size = -100;
    *reinterpret_cast<unsigned char*>(&invalid.esp.enabled) = 0x7F;
    validate_config(invalid);
    check(invalid.aimbot.fov == 80 && invalid.aimbot.bone == 4 && invalid.esp.minimap_size == 120 && invalid.esp.enabled, "settings validation");
    config.aimbot.fov = 135;
    config.esp.visible_color[1] = 0.25f;
    config.colors.radar_grid[2] = 0.35f;
    config.esp.minimap_x = 320;
    config.esp.minimap_y = 140;
    config.selected_visible_color[0] = 0.4f;
    config.magic_ignore_visibility = false;
    check(save_config(), "save isolated test settings");
    config = {};
    check(load_config() && config.aimbot.fov == 135 && config.esp.visible_color[1] == 0.25f && config.colors.radar_grid[2] == 0.35f && config.esp.minimap_x == 320 && config.esp.minimap_y == 140, "settings, palette and radar position round trip");
    check(config.selected_visible_color[0] == 0.4f, "visible target color survives save and load");
    check(!config.magic_ignore_visibility, "disabled Magic visibility bypass survives save and load");
    unsigned char legacy[188]{};
    const DWORD version = 1;
    memcpy(legacy, &version, sizeof(version));
    const float old_white[]{1, 1, 1, 1};
    memcpy(config.esp.visible_color, old_white, sizeof(old_white));
    config.esp.vehicle_color[0] = 0.7f;
    memcpy(legacy + 4, &config, 184);
    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\WDRewriteTests", "Settings", REG_BINARY, legacy, sizeof(legacy));
    config = {};
    check(load_config() && config.aimbot.fov == 135 && config.esp.visible_color[0] == 0 && config.esp.visible_color[1] == 1 && config.esp.vehicle_color[0] == 0.7f && config.colors.radar_grid[2] == 1 && config.esp.minimap_x == 320, "version 1 migration keeps custom settings and upgrades unchanged colors");
    legacy[0] = 2;
    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\WDRewriteTests", "Settings", REG_BINARY, legacy, sizeof(legacy));
    check(!load_config() && config.aimbot.fov == 135, "truncated version 2 settings are rejected");
    unsigned char v2[4 + offsetof(Config, extra)]{};
    v2[0] = 2;
    memcpy(v2 + 4, &config, offsetof(Config, extra));
    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\WDRewriteTests", "Settings", REG_BINARY, v2, sizeof(v2));
    config.extra.no_recoil = true;
    check(load_config() && config.extra.explosives && !config.extra.no_recoil && !config.extra.auto_join, "version 2 migration supplies new defaults without overwriting old settings");
    config.esp.minimap_size = 200;
    config.esp.minimap_x = screen_width - 112.f;
    config.esp.minimap_y = 112;
    config.extra.build_x = true;
    unsigned char v3[4 + offsetof(Config, radar)]{};
    v3[0] = 3;
    memcpy(v3 + 4, &config, offsetof(Config, radar));
    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\WDRewriteTests", "Settings", REG_BINARY, v3, sizeof(v3));
    check(load_config() && config.esp.minimap_size == 400 && config.esp.minimap_x == -1 && config.esp.minimap_y == -1 && config.extra.build_x, "version 3 migration doubles the old default radar and preserves feature settings");
    config.radar.items = true;
    config.radar.helicopters = false;
    config.colors.selected[0] = 0.6f;
    unsigned char v4[4 + offsetof(Config, selected_visible_color)]{4};
    memcpy(v4 + 4, &config, offsetof(Config, selected_visible_color));
    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\WDRewriteTests", "Settings", REG_BINARY, v4, sizeof(v4));
    config.selected_visible_color[0] = 1;
    check(load_config() && config.radar.items && !config.radar.helicopters && config.colors.selected[0] == 0.6f && config.selected_visible_color[0] == 0 && config.selected_visible_color[1] == 1 && config.selected_visible_color[2] == 1, "version 4 keeps custom hidden target color and defaults visible targets to cyan");
    config.selected_visible_color[0] = 0.25f;
    unsigned char v5[4 + offsetof(Config, magic_ignore_visibility)]{5};
    memcpy(v5 + 4, &config, offsetof(Config, magic_ignore_visibility));
    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\WDRewriteTests", "Settings", REG_BINARY, v5, sizeof(v5));
    config.magic_ignore_visibility = false;
    check(load_config() && config.magic_ignore_visibility && config.selected_visible_color[0] == 0.25f && config.colors.selected[0] == 0.6f && config.radar.items && !config.radar.helicopters, "version 5 settings default Magic visibility bypass on and retain colors and radar");
    check(save_config(), "save version 6 settings");
    config.radar.items = false;
    config.radar.helicopters = true;
    check(load_config() && config.radar.items && !config.radar.helicopters, "radar filters survive save and load");
    DWORD broken = 999;
    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\WDRewriteTests", "Settings", REG_BINARY, &broken, sizeof(broken));
    check(!load_config() && config.aimbot.fov == 135, "bad settings leave current state unchanged");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\WDRewriteTests");
    config = {};

    prediction::Input input;
    input.target = {100000, 0, 0};
    input.bullet_speed = 100000;
    input.bullet_drop = false;
    input.velocity_lead = false;
    auto result = prediction::solve(input);
    check(result.valid && std::fabs(result.travel_time - 1) < 0.001 && result.aim_point.X == 100000 && result.aim_point.Z == 0, "stationary trajectory");
    input.velocity_lead = true;
    input.target_velocity = {0, 1000, 0};
    result = prediction::solve(input);
    check(result.valid && result.aim_point.Y > 999 && result.aim_point.Y < 1001, "velocity lead");
    input.bullet_speed = 0;
    check(!prediction::solve(input).valid, "invalid trajectory rejected");

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WDRewriteTest";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"WD Rewrite Test", WS_POPUP, 0, 0, 900, 620, nullptr, nullptr, wc.hInstance, nullptr);
    check(window != nullptr && overlay::initialize(window), "renderer initializes on disposable window");
    if (!overlay::window())
    {
        DestroyWindow(window);
        return 1;
    }
    check(begin_frame(), "begin transparent frame");
    overlay::rect(10, 10, 20, 20, {1, 0, 0, 0.5f});
    check(overlay::end(), "DX9 presentation");
    check(overlay::capture(L"build/alpha.bmp"), "capture presentation buffer");
    check(pixel(L"build/alpha.bmp", 0, 0, 0, 0, 0, 0), "transparent background preserved");
    check(pixel(L"build/alpha.bmp", 15, 15, 128, 0, 0, 128), "premultiplied alpha preserved");

    LARGE_INTEGER blocked_start, blocked_end, blocked_frequency;
    QueryPerformanceFrequency(&blocked_frequency);
    for (unsigned stage = 1; stage <= 3; ++stage)
    {
        check(begin_frame(), "begin busy-GPU regression frame");
        overlay::rect(10, 10, 20, 20, {1, 0, 0, 1});
        overlay::test_block_gpu(stage);
        QueryPerformanceCounter(&blocked_start);
        const bool queued = overlay::end();
        QueryPerformanceCounter(&blocked_end);
        const double blocked_ms = 1000.0 * (blocked_end.QuadPart - blocked_start.QuadPart) / blocked_frequency.QuadPart;
        check(queued && overlay::window() && blocked_ms < 20.0, "busy GPU queues work without blocking the caller");
        printf("Busy-GPU stage %u submission: %.3f ms\n", stage, blocked_ms);
        const auto wait_start = GetTickCount64();
        double poll_max = 0;
        while (!overlay::test_gpu_blocked() && GetTickCount64() - wait_start < 500)
        {
            QueryPerformanceCounter(&blocked_start);
            overlay::flush();
            QueryPerformanceCounter(&blocked_end);
            poll_max = (std::max)(poll_max, 1000.0 * (blocked_end.QuadPart - blocked_start.QuadPart) / blocked_frequency.QuadPart);
            Sleep(1);
        }
        check(overlay::test_gpu_blocked() && poll_max < 20.0, "draw, copy or present remains pending without a wait loop");
        check(overlay::begin(), "second buffer available while first is busy");
        overlay::rect(10, 10, 20, 20, {0, 1, 0, 1});
        check(overlay::end(), "queue second frame while GPU busy");
        QueryPerformanceCounter(&blocked_start);
        const bool skipped = !overlay::begin();
        QueryPerformanceCounter(&blocked_end);
        check(skipped && overlay::window() && 1000.0 * (blocked_end.QuadPart - blocked_start.QuadPart) / blocked_frequency.QuadPart < 20.0, "two busy buffers skip drawing without blocking or disconnecting");
        overlay::test_block_gpu(0);
        check(overlay::capture(L"build/busy.bmp"), "deferred frames eventually complete");
        check(pixel(L"build/busy.bmp", 15, 15, 0, 255, 0, 255), "queued frames preserve order and final pixels");
    }

    failures += test_effect_drawing();
    for (int tab = 0; tab < 8; ++tab)
    {
        menu::test_input(tab, 0, 0, false);
        check(begin_frame(), "begin menu frame");
        menu::draw();
        check(overlay::end(), "present menu frame");
        wchar_t path[64];
        swprintf_s(path, L"build/menu-%d.bmp", tab);
        check(overlay::capture(path), "capture menu tab");
    }
    const bool saved_build_x = config.extra.build_x;
    const bool saved_auto_join = config.extra.auto_join;
    config.extra.build_x = false;
    config.extra.auto_join = true;
    menu::test_input(5, 665, 250, true);
    check(begin_frame(), "begin Build X diagnostic menu frame");
    menu::draw();
    check(config.extra.build_x, "Build X toggle changes the setting before callback delivery");
    overlay::end();
    check(overlay::capture(L"build/build-x-toggle.bmp"), "capture the Build X toggle frame before its settled state");
    menu::test_input(5, 0, 0, false);
    check(begin_frame(), "begin settled Build X diagnostic menu frame");
    menu::draw();
    check(overlay::end() && overlay::capture(L"build/build-x-status.bmp"), "Build X and faction diagnostic labels render together");
    config.extra.build_x = saved_build_x;
    config.extra.auto_join = saved_auto_join;
    menu::test_input(0, 340, 160, true);
    check(begin_frame(), "begin toggle frame");
    menu::draw();
    overlay::end();
    check(!config.aimbot.enabled, "toggle changes actual setting");
    menu::test_input(0, 0, 0, false);
    check(begin_frame(), "begin unchanged toggle frame");
    menu::draw();
    overlay::end();
    check(!config.aimbot.enabled, "toggle does not repeat without another click");
    menu::test_input(0, 358, 340, true);
    check(begin_frame(), "begin numeric setting frame");
    menu::draw();
    overlay::end();
    check(config.aimbot.fov == 85, "numeric button changes actual setting");

    const auto draw_menu = [&]()
    {
        if (!begin_frame())
            return false;
        menu::draw();
        return overlay::end();
    };
    menu::test_input(0, 0, 0, false);
    menu::test_mouse(190, 116, true);
    for (int i = 0; i < 1000; ++i)
        menu::test_mouse(336, 116, true);
    menu::test_mouse(336, 116, false);
    check(draw_menu() && menu::test_tab() == 1, "tab press survives 1000 polls without drawing, release and cursor movement");
    menu::test_mouse(270, 116, true);
    check(draw_menu() && menu::test_tab() == 2, "next tab responds without a cooldown");
    menu::test_mouse(270, 116, false);
    const bool vehicles = config.esp.vehicles;
    menu::test_mouse(340, 160, true);
    for (int i = 0; i < 1000; ++i)
        menu::test_mouse(340, 160, true);
    check(draw_menu() && config.esp.vehicles != vehicles, "toggle after tab change survives skipped menu frames");
    menu::test_mouse(340, 160, true);
    check(draw_menu() && config.esp.vehicles != vehicles, "held button does not repeat on the next menu frame");
    menu::test_mouse(340, 160, false);
    menu::test_mouse(340, 160, true);
    check(draw_menu() && config.esp.vehicles == vehicles, "release and press toggles again immediately");
    menu::test_mouse(340, 160, false);
    menu::test_mouse(800, 570, true);
    check(draw_menu(), "discard a click outside the menu after one frame");
    menu::test_mouse(340, 160, true);
    check(draw_menu() && config.esp.vehicles == vehicles, "outside click cannot activate a control after cursor movement");
    menu::test_mouse(340, 160, false);
    menu::test_input(3, 0, 0, false);
    menu::test_mouse(288, 310, true);
    menu::test_mouse(800, 570, false);
    check(draw_menu() && config.esp.minimap_x == 450, "released slider press retains its original position across skipped frames");
    menu::test_mouse(1000, 310, false);
    check(draw_menu() && config.esp.minimap_x == 450, "released pending slider does not keep dragging");

    const auto menu_frame = [&](int tab, float x, float y, bool click, bool down = false)
    {
        menu::test_input(tab, x, y, click, down);
        return draw_menu();
    };
    check(menu_frame(3, 288, 310, true) && config.esp.minimap_x == 450, "radar X slider sets center coordinate");
    check(menu_frame(3, 1000, 310, false, true) && config.esp.minimap_x == 700, "radar slider drag clamps to right edge");
    check(menu_frame(3, 0, 310, false, true) && config.esp.minimap_x == 200, "radar slider drag clamps to left edge");
    check(menu_frame(3, 288, 340, false) && config.esp.minimap_x == 200, "released radar slider stops changing");
    check(menu_frame(3, 288, 340, true) && config.esp.minimap_y == 310, "radar Y slider moves independently");
    check(menu_frame(3, 250, 460, true) && config.esp.minimap_x == -1 && config.esp.minimap_y == -1, "reset radar restores automatic top right position");
    check(menu_frame(6, 288, 248, true) && config.esp.visible_color[0] == 0.5f, "color channel slider changes player color");
    menu_frame(6, 800, 570, false);
    config = {};

    game::Snapshot scene;
    scene.valid = true;
    scene.camera.rotation.Yaw = 0;
    scene.local_yaw_valid = true;
    game::ProjectedPlayer p{};
    p.actor_addr = 1;
    p.player.health = 75;
    p.player.max_health = 100;
    p.player.distance = 35;
    p.player.isVisible = true;
    p.player.world_pos = {3000, 1200, 0};
    wcscpy_s(p.player.player_name, L"Player / \u6d4b\u8bd5");
    p.screen = {400, 310, true};
    p.bones[BONE_ROOT] = {400, 380, true};
    p.player.has_bones = true;
    p.bones[BONE_HEAD] = {400, 240, true};
    p.bones[BONE_NECK] = {400, 260, true};
    p.bones[BONE_CHEST] = {400, 280, true};
    p.bones[BONE_PELVIS] = {400, 318, true};
    p.bones[BONE_CLAVICLE_L] = {389, 265, true};
    p.bones[BONE_CLAVICLE_R] = {411, 265, true};
    p.bones[BONE_UPPER_ARM_L] = {378, 275, true};
    p.bones[BONE_UPPER_ARM_R] = {422, 275, true};
    p.bones[BONE_LOWER_ARM_L] = {373, 296, true};
    p.bones[BONE_LOWER_ARM_R] = {427, 296, true};
    p.bones[BONE_HAND_L] = {379, 310, true};
    p.bones[BONE_HAND_R] = {434, 308, true};
    p.bones[BONE_THIGH_L] = {389, 326, true};
    p.bones[BONE_THIGH_R] = {411, 326, true};
    p.bones[BONE_CALF_L] = {385, 350, true};
    p.bones[BONE_CALF_R] = {417, 350, true};
    p.bones[BONE_FOOT_L] = {381, 378, true};
    p.bones[BONE_FOOT_R] = {424, 378, true};
    scene.players.push_back(p);
    game::ProjectedWorldActor vehicle{};
    strcpy_s(vehicle.actor.label, "Talon 9K-SAM");
    vehicle.actor.distance_meters = 250;
    vehicle.actor.kind = wdgs::actors::Kind::stationary;
    vehicle.actor.world_position = {23000, 4000, 0};
    vehicle.screen = {610, 210, true};
    scene.vehicles.push_back(vehicle);
    scene.anti_sam.incoming = true;
    scene.anti_sam.distance_meters = 180;
    menu_open = false;
    check(begin_frame(), "begin scene frame");
    visuals::draw(scene);
    overlay::end();
    check(overlay::capture(L"build/scene.bmp"), "feature drawing with sample data");
    check(pixel(L"build/scene.bmp", 500, 24, 0, 0, 0, 0) && pixel(L"build/scene.bmp", 688, 212, 255, 255, 255, 255), "round radar has transparent corners and a top right center");
    for (int mode : {0, 2, 3, 4})
    {
        config.extra.player_text = mode;
        check(begin_frame(), "begin player label mode");
        visuals::draw(scene);
        overlay::end();
        wchar_t path[64];
        swprintf_s(path, L"build/player-text-%d.bmp", mode);
        check(overlay::capture(path), "capture player label mode");
    }
    config.extra.player_text = 0;

    const Config before_camera_test = config;
    config.esp.vehicles = config.esp.loot = config.esp.minimap = false;
    config.aimbot.draw_fov = config.anti_sam.flare_warning = false;
    auto camera_scene = scene;
    camera_scene.camera.fov = 90;
    auto& fixed_player = camera_scene.players[0];
    const auto unproject = [&](const game::ScreenPoint& point)
    { return FVector{3000, (point.x - screen_width * 0.5) * 3000 / (screen_width * 0.5), (screen_height * 0.5 - point.y) * 3000 / (screen_width * 0.5)}; };
    fixed_player.player.world_pos = unproject(fixed_player.screen);
    for (int i = 0; i < BONE_COUNT; ++i)
        fixed_player.player.bones[i] = unproject(fixed_player.bones[i]);
    for (int turn = -1; turn <= 1; ++turn)
    {
        CameraIPC view = camera_scene.camera;
        view.rotation.Yaw = turn * 12;
        check(begin_frame(), "begin late camera projection frame");
        visuals::draw(camera_scene, &view);
        overlay::end();
        wchar_t path[64];
        swprintf_s(path, L"build/camera-pan-%d.bmp", turn + 1);
        check(overlay::capture(path), "camera turn renders from the same frozen entity snapshot");
    }
    config = before_camera_test;

    config.esp.minimap_auto_range = false;
    scene.players[0].player.world_pos = {0, 30000, 0};
    check(begin_frame(), "begin radar boundary frame");
    visuals::draw(scene);
    overlay::end();
    check(overlay::capture(L"build/radar-boundary.bmp") && pixel(L"build/radar-boundary.bmp", 881, 212, 0, 255, 0, 255) && pixel(L"build/radar-boundary.bmp", 893, 212, 0, 0, 0, 0), "edge marker stays inside round radar");
    config.esp.minimap_x = 212;
    config.esp.minimap_y = 408;
    check(begin_frame(), "begin moved radar frame");
    visuals::draw(scene);
    overlay::end();
    check(overlay::capture(L"build/radar-moved.bmp") && pixel(L"build/radar-moved.bmp", 212, 408, 255, 255, 255, 255) && pixel(L"build/radar-moved.bmp", 688, 212, 0, 0, 0, 0), "radar moves without leaving its previous image");

    const Config before_filters = config;
    config = {};
    config.esp.enabled = config.esp.vehicles = config.esp.loot = false;
    config.extra.explosives = config.extra.death_bags = false;
    config.aimbot.draw_fov = config.anti_sam.flare_warning = false;
    config.esp.minimap_auto_range = config.extra.radar_smoothing = false;
    config.colors.radar_fill[3] = config.colors.radar_grid[3] = config.colors.radar_border[3] = 0;
    game::Snapshot radar_scene;
    radar_scene.valid = true;
    const FVector marker_position{0, 15000, 0};
    const auto capture_radar = [&]()
    {
        if (!begin_frame())
            return false;
        visuals::draw(radar_scene);
        return overlay::end() && overlay::capture(L"build/radar-filters.bmp");
    };
    const auto filter_check = [&](bool& filter, const char* name)
    {
        filter = true;
        const bool shown = capture_radar() && !pixel(L"build/radar-filters.bmp", 785, 212, 0, 0, 0, 0);
        filter = false;
        check(shown && capture_radar() && pixel(L"build/radar-filters.bmp", 785, 212, 0, 0, 0, 0), name);
    };
    game::ProjectedPlayer contact;
    contact.player.health = 100;
    contact.player.isVisible = true;
    contact.player.distance = 150;
    contact.player.world_pos = marker_position;
    radar_scene.players.push_back(contact);
    filter_check(config.radar.enemies, "radar enemy filter works with player ESP disabled");
    radar_scene.players[0].player.is_in_team = true;
    filter_check(config.extra.radar_team, "radar teammate filter works independently");
    radar_scene.players[0].player.is_in_team = false;
    radar_scene.players[0].player.health = 0;
    config.radar.enemies = true;
    filter_check(config.radar.downed, "radar downed filter includes or hides zero-health players");
    radar_scene.players.clear();
    game::ProjectedWorldActor radar_actor;
    radar_actor.actor.world_position = marker_position;
    radar_actor.actor.distance_meters = 150;
    radar_scene.vehicles.push_back(radar_actor);
    using wdgs::actors::Kind;
    const Kind kinds[]{Kind::heli, Kind::buggy, Kind::boat, Kind::stationary};
    bool* vehicle_filters[]{&config.radar.helicopters, &config.radar.ground, &config.radar.boats, &config.radar.stationary};
    for (int i = 0; i < 4; ++i)
    {
        radar_scene.vehicles[0].actor.kind = kinds[i];
        filter_check(*vehicle_filters[i], "radar vehicle category works with vehicle ESP disabled");
    }
    radar_scene.vehicles.clear();
    radar_scene.dropped_items.push_back(radar_actor);
    filter_check(config.radar.items, "radar item filter works with item ESP disabled");
    radar_scene.dropped_items.clear();
    game::Snapshot::Marker radar_marker;
    radar_marker.world = marker_position;
    radar_marker.distance = 150;
    radar_scene.markers.push_back(radar_marker);
    filter_check(config.radar.explosives, "radar explosive filter works with explosive ESP disabled");
    radar_scene.markers[0].bag = true;
    filter_check(config.radar.bags, "radar bag filter works with bag ESP disabled");
    radar_scene.markers.clear();
    radar_scene.players.push_back(contact);
    radar_scene.players[0].player.world_pos.Y = 10000;
    radar_scene.players[0].player.distance = 100;
    radar_actor.actor.kind = Kind::heli;
    radar_actor.actor.world_position.Y = 500000;
    radar_actor.actor.distance_meters = 5000;
    radar_scene.vehicles.push_back(radar_actor);
    config.esp.minimap_auto_range = true;
    check(capture_radar() && pixel(L"build/radar-filters.bmp", 842, 212, 0, 255, 0, 255), "hidden distant categories do not expand automatic radar range");
    config = before_filters;

    Config scene_settings = config;
    config.esp.lines = config.esp.skeleton = config.esp.health = config.esp.agent_name = config.esp.distance = config.esp.minimap = config.esp.vehicles = config.esp.loot = false;
    config.aimbot.draw_fov = config.anti_sam.flare_warning = false;
    scene.players[0].bones[BONE_HEAD] = {420, 200, true};
    for (int style = 0; style < 2; ++style)
    {
        config.esp.box_style = style;
        check(begin_frame(), "begin box geometry frame");
        visuals::draw(scene);
        overlay::end();
        wchar_t path[64];
        swprintf_s(path, L"build/box-%d.bmp", style);
        check(overlay::capture(path) && pixel(path, 460, 280, 10, 10, 21, 138) && pixel(path, 470, 280, 0, 0, 0, 0), "box uses head/root height and half-height width without skeleton drawing");
    }
    scene.mortar.valid = false;
    scene.aim_selected_actor = scene.players[0].actor_addr;
    scene.players[0].bones[BONE_HEAD] = {420.5f, 200.5f, true};
    scene.players[0].bones[BONE_ROOT] = {420.5f, 380.5f, true};
    const auto capture_target = [&]()
    {
        if (!begin_frame())
            return false;
        visuals::draw(scene);
        overlay::end();
        return overlay::capture(L"build/target-color.bmp");
    };
    scene.players[0].player.isVisible = true;
    check(capture_target() && pixel(L"build/target-color.bmp", 420, 200, 0, 255, 255, 255), "visible selected player is cyan");
    scene.players[0].player.isVisible = false;
    config.esp.visible_check = false;
    check(capture_target() && pixel(L"build/target-color.bmp", 420, 200, 255, 255, 0, 255), "hidden selected player is yellow even with ESP visibility filtering off");
    scene.players[0].player.isVisible = true;
    config.selected_visible_color[0] = 1;
    config.selected_visible_color[1] = 0;
    check(capture_target() && pixel(L"build/target-color.bmp", 420, 200, 255, 0, 255, 255), "visible target uses the picked color");
    scene.aim_selected_actor = 0;
    check(capture_target() && pixel(L"build/target-color.bmp", 420, 200, 0, 255, 0, 255), "releasing target selection restores the normal player color");
    config = scene_settings;
    check(begin_frame(), "begin empty world frame");
    scene.valid = false;
    visuals::draw(scene);
    overlay::end();
    overlay::capture(L"build/empty.bmp");
    check(pixel(L"build/empty.bmp", 610, 210, 0, 0, 0, 0), "world transition clears previous drawing");

    SetWindowPos(window, nullptr, 0, 0, 1024, 768, SWP_NOZORDER | SWP_NOACTIVATE);
    check(begin_frame() && screen_width == 900 && screen_height == 620, "drawing retains the dimensions read at initialization");
    overlay::rect(5, 5, 20, 20, {0, 1, 0, 1});
    check(overlay::end(), "present with cached dimensions");
    overlay::capture(L"build/fixed-size.bmp");
    check(pixel(L"build/fixed-size.bmp", 10, 10, 0, 255, 0, 255), "fixed target content");
    overlay::shutdown();
    check(overlay::initialize(window) && screen_width == 1024 && screen_height == 768, "explicit initialization reads dimensions once");
    LARGE_INTEGER start, finish, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    bool frames_ok = true;
    double submit_total = 0, submit_max = 0;
    for (int i = 0; i < 120; ++i)
    {
        frames_ok &= begin_frame();
        QueryPerformanceCounter(&blocked_start);
        menu::test_input(i % 8, 800, 570, false);
        menu::draw();
        frames_ok &= overlay::end();
        QueryPerformanceCounter(&blocked_end);
        const double elapsed = 1000.0 * (blocked_end.QuadPart - blocked_start.QuadPart) / frequency.QuadPart;
        submit_total += elapsed;
        submit_max = (std::max)(submit_max, elapsed);
    }
    QueryPerformanceCounter(&finish);
    check(frames_ok, "120 consecutive frames");
    printf("Menu draw/submit: %.3f ms CPU average, %.3f ms max; %.3f ms/frame including test-only waits (no gameplay)\n", submit_total / 120, submit_max, 1000.0 * (finish.QuadPart - start.QuadPart) / frequency.QuadPart / 120);
    check(begin_frame() && overlay::end() && overlay::capture(L"build/cleared.bmp"), "drain a blank frame before stopping");
    check(pixel(L"build/cleared.bmp", 100, 100, 0, 0, 0, 0), "stopping clears the previous menu");
    overlay::shutdown();
    overlay::shutdown();
    check(!overlay::begin(), "shutdown is repeatable");
    check(!overlay::initialize(nullptr), "missing overlay handled");
    DestroyWindow(window);

    wchar_t executable[MAX_PATH], command[MAX_PATH + 80], title[64];
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    swprintf_s(command, L"\"%ls\" --overlay-host %lu", executable, GetCurrentProcessId());
    swprintf_s(title, L"WD Rewrite Test %lu", GetCurrentProcessId());
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    const bool launched = CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != FALSE;
    check(launched, "launch separate overlay owner");
    if (launched)
    {
        HWND external = nullptr;
        for (int i = 0; i < 100 && !external; ++i)
        {
            external = FindWindowW(L"WDRewriteChild", title);
            if (!external)
                Sleep(20);
        }
        bool attached = false;
        if (external)
        {
            // Discovery can see the HWND before its owner finishes creating it.
            for (int i = 0; i < 20 && !attached; ++i)
            {
                attached = overlay::initialize(external);
                if (!attached)
                    Sleep(50);
            }
        }
        if (!attached)
        {
            DWORD code;
            GetExitCodeProcess(child.hProcess, &code);
            printf("External HWND=%p, child exit=%lu, last error=%lu\n", external, code, GetLastError());
        }
        check(attached, "attach to another process's window");
        if (overlay::window())
        {
            check(begin_frame(), "begin external frame");
            overlay::rect(10, 10, 40, 40, {0, 0, 1, 0.5f});
            check(overlay::end(), "present external frame");
            check(overlay::capture(L"build/external.bmp") && pixel(L"build/external.bmp", 20, 20, 0, 0, 128, 128), "external presentation preserves pixels and alpha");
            overlay::shutdown();
        }
        if (external)
            PostMessageW(external, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(child.hProcess, 3000) != WAIT_OBJECT_0)
            TerminateProcess(child.hProcess, 3);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    }
    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
