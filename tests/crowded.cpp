#include "../src/config.h"
#include "../src/overlay.h"
#include "../src/visuals.h"
#include <array>

namespace
{
    bool begin_frame()
    {
        const auto start = GetTickCount64();
        while (!overlay::begin())
        {
            if (!overlay::window() || GetTickCount64() - start > 3000)
                return false;
            Sleep(1);
        }
        return true;
    }

    game::Snapshot crowd(int count)
    {
        game::Snapshot scene;
        scene.valid = true;
        scene.players.reserve(count);
        const float bone_x[BONE_COUNT]{0, 0, 0, 0, -12, -20, -22, 12, 20, 22, -7, -8, -9, 7, 8, 9, -7, 7, 0};
        const float bone_y[BONE_COUNT]{-110, -98, -82, -52, -87, -68, -48, -87, -68, -48, -50, -28, 0, -50, -28, 0, -92, -92, 0};
        for (int i = 0; i < count; ++i)
        {
            game::ProjectedPlayer p{};
            p.actor_addr = i + 1;
            p.player.distance = 30.f + i;
            p.player.health = 75;
            p.player.max_health = 100;
            p.player.has_bones = true;
            p.player.isVisible = i % 2 == 0;
            swprintf_s(p.player.player_name, L"Player_%03d", i);
            p.screen = {65.f + (i % 16) * 116.f, 137.f + (i / 16) * 124.f, true};
            for (int b = 0; b < BONE_COUNT; ++b)
                p.bones[b] = {p.screen.x + bone_x[b], p.screen.y + bone_y[b], true};
            scene.players.push_back(p);
        }
        return scene;
    }
} // namespace

int benchmark_crowd(bool effects, bool camera_pan)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WD_Crowd_Benchmark";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"WD crowd benchmark", WS_POPUP, 0, 0, 1920, 1080, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window || !overlay::initialize(window))
        return 1;
    config = {};
    config.esp.enabled = config.esp.box = config.esp.skeleton = config.esp.health = config.esp.agent_name = config.esp.distance = true;
    config.esp.player_distance = config.esp.skeleton_distance = 2000;
    config.esp.minimap = config.esp.lines = config.esp.vehicles = config.esp.loot = false;
    config.aimbot.draw_fov = false;
    config.extra.tracers = effects;
    config.anti_sam.flare_warning = false;
    menu_open = false;
    LARGE_INTEGER frequency, begin, end, wall_start, wall_end;
    QueryPerformanceFrequency(&frequency);
    const double milliseconds = 1000.0 / frequency.QuadPart;
    int failures = 0;
    printf("1920x1080, Tahoma, corner boxes, full skeletons, health, names and distances. Synthetic scene; no game update.\n");
    if (camera_pan)
        printf("Fresh camera projection each draw; frozen entity publication during left/right camera sweeps.\n");
    for (int count : {16, 64, 128})
        for (int moving = 0; moving < 2; ++moving)
        {
            auto scene = crowd(count);
            scene.camera.fov = 90;
            if (camera_pan)
                for (auto& p : scene.players)
                {
                    const auto world = [&](const game::ScreenPoint& point)
                    { return FVector{10000, (point.x - screen_width * 0.5) * 10000 / (screen_width * 0.5), (screen_height * 0.5 - point.y) * 10000 / (screen_width * 0.5)}; };
                    p.player.world_pos = world(p.screen);
                    for (int i = 0; i < BONE_COUNT; ++i)
                        p.player.bones[i] = world(p.bones[i]);
                }
            if (effects)
            {
                scene.camera.fov = 90;
                for (int i = 0; i < 64; ++i)
                {
                    game::Snapshot::Marker marker;
                    marker.world = {5000, -4000.0 + (i % 16) * 500, -2000.0 + (i / 16) * 1000};
                    marker.distance = 50;
                    marker.bag = i % 2 != 0;
                    marker.timed = !marker.bag;
                    marker.fuse_left = 2;
                    marker.fuse_total = 5;
                    marker.nearby = marker.bag ? 1 : 0;
                    strcpy_s(marker.label, marker.bag ? "DEATH BAG" : "GRENADE");
                    scene.markers.push_back(marker);
                }
                for (int i = 0; i < 24; ++i)
                {
                    game::Snapshot::Trail trail;
                    trail.count = 12;
                    trail.hue = i / 24.f;
                    for (int j = 0; j < trail.count; ++j)
                        trail.points[j] = {1500.0 + j * 100, -500.0 + i * 30, -400.0 + j * 60};
                    scene.trails.push_back(trail);
                }
            }
            std::array<double, 60> samples{};
            unsigned created = 0;
            QueryPerformanceCounter(&wall_start);
            for (int frame = camera_pan ? -60 : -4; frame < static_cast<int>(samples.size()); ++frame)
            {
                if (moving)
                    for (int i = 0; i < count; ++i)
                        scene.players[i].player.distance = 30.f + i + std::max(frame, -4) + 4;
                if (!begin_frame())
                {
                    ++failures;
                    break;
                }
                if (frame == 0)
                {
                    created = overlay::test_text_layouts();
                    QueryPerformanceCounter(&wall_start);
                }
                QueryPerformanceCounter(&begin);
                CameraIPC camera = scene.camera;
                camera.rotation.Yaw = ((frame + 60) % 60 - 30) * 0.15;
                visuals::draw(scene, camera_pan ? &camera : nullptr);
                if (!overlay::end())
                    ++failures;
                QueryPerformanceCounter(&end);
                if (frame >= 0)
                    samples[frame] = (end.QuadPart - begin.QuadPart) * milliseconds;
            }
            QueryPerformanceCounter(&wall_end);
            created = overlay::test_text_layouts() - created;
            if (!moving && created != 0)
            {
                printf("FAIL: unchanged crowd labels rebuilt after warmup\n");
                ++failures;
            }
            double total = 0;
            for (double sample : samples)
                total += sample;
            std::sort(samples.begin(), samples.end());
            printf("%3d players, %s labels: draw/submit mean %.3f ms, median %.3f, p95 %.3f, max %.3f; %.1f layouts/frame; wall %.3f ms/frame including test waits\n", count, moving ? "moving" : "static", total / samples.size(), samples[30], samples[56], samples[59], created / double(samples.size()), (wall_end.QuadPart - wall_start.QuadPart) * milliseconds / samples.size());
            if (count == 128 && !moving && !overlay::capture(L"build/crowded.bmp"))
                ++failures;
        }
    overlay::shutdown();
    DestroyWindow(window);
    printf("Crowd benchmark: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
