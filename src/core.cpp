#include "stdafx.h"
#include "config.h"
#include "game.h"
#include "frame_exchange.h"
#include "menu.h"
#include "offsets.h"
#include "overlay.h"
#include "visuals.h"
#include "game_actions.h"

namespace
{
    FrameExchange frames;

    DWORD WINAPI update(void*)
    {
        game::init();
        Config settings;
        bool visible;
        game::Snapshot next;
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        const double seconds_per_count = 1.0 / frequency.QuadPart;
        while (frames.read_inputs(settings, visible))
        {
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            frame_time = counter.QuadPart * seconds_per_count;
            frame_ticks = static_cast<ULONGLONG>(frame_time * 1000.0);
            game::tick(next, settings, visible);
            if (!frames.publish(next))
                break;
        }
        game_actions::stop();
        game::shutdown();
        return 0;
    }

    DWORD WINAPI run(void*)
    {
        // log("startup");
        HMODULE self;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCSTR>(&run), &self);
        if (!overlay::initialize(FindWindowA("GLFW30", "Echo Overlay")))
        {
            // log("overlay initialization failed");
            return 1;
        }
        // log("overlay ready: %dx%d", screen_width, screen_height);
        offsets::setup();
        load_config();
        game::Snapshot displayed;
        frames.refresh(displayed, config, menu_open);
        HANDLE updater = CreateThread(nullptr, 0, update, nullptr, 0, nullptr);
        if (!updater)
        {
            // log("update thread creation failed");
            overlay::shutdown();
            return 2;
        }
        // log("update and render threads running");
        for (;;)
        {
            menu::update();
            if (menu::stop_requested())
                break;
            const bool drawing = overlay::begin();
            frames.refresh(displayed, config, menu_open);
            if (drawing)
            {
                CameraIPC camera{};
                if (game::camera_for_render(displayed, camera))
                    visuals::draw(displayed, &camera);
                menu::draw();
                if (!overlay::end())
                    break;
            }
            else if (!overlay::window())
                break;
        }
        frames.stop();
        WaitForSingleObject(updater, INFINITE);
        CloseHandle(updater);
        // Only shutdown drains the GPU. The running loop never waits for it.
        const auto stop_time = GetTickCount64();
        bool cleared = false;
        while (overlay::window() && GetTickCount64() - stop_time < 1000)
        {
            if (!cleared)
            {
                if (overlay::begin())
                    cleared = overlay::end();
            }
            else if (overlay::flush())
                break;
        }
        overlay::shutdown();
        // log("stopped");
        return 0;
    }
} // namespace

BOOL WINAPI DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        // log("process attach");

        run(0);

        // HANDLE thread = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
        // if (!thread)
        //{
        //    // log("startup thread creation failed");
        //     return FALSE;
        // }
        // CloseHandle(thread);
    }
    return TRUE;
}
