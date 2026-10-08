#include "../src/frame_exchange.h"

namespace
{
    void fill(game::Snapshot& snapshot, unsigned value)
    {
        snapshot.valid = true;
        snapshot.camera.location.X = value;
        snapshot.players.resize(16 + value % 113);
        for (auto& player : snapshot.players)
        {
            player.actor_addr = value;
            player.player.health = static_cast<float>(value);
            player.bones[BONE_HEAD].x = static_cast<float>(value);
        }
        snapshot.vehicles.resize(3);
        for (auto& vehicle : snapshot.vehicles)
            vehicle.actor.address = value;
        snapshot.dropped_items.resize(2);
        for (auto& item : snapshot.dropped_items)
            item.actor.address = value;
        snapshot.markers.resize(3);
        for (auto& marker : snapshot.markers)
            marker.actor = value;
        snapshot.trails.resize(2);
        for (auto& trail : snapshot.trails)
        {
            trail.count = 2;
            trail.points[0].X = value;
        }
        snapshot.mortar.candidates.resize(4);
        for (auto& candidate : snapshot.mortar.candidates)
            candidate.actor_addr = value;
    }

    bool consistent(const game::Snapshot& snapshot)
    {
        const auto value = static_cast<unsigned>(snapshot.camera.location.X);
        if (!snapshot.valid || snapshot.players.size() != 16 + value % 113 || snapshot.vehicles.size() != 3 || snapshot.dropped_items.size() != 2 || snapshot.mortar.candidates.size() != 4)
            return false;
        for (const auto& player : snapshot.players)
            if (player.actor_addr != value || player.player.health != value || player.bones[BONE_HEAD].x != value)
                return false;
        for (const auto& vehicle : snapshot.vehicles)
            if (vehicle.actor.address != value)
                return false;
        for (const auto& item : snapshot.dropped_items)
            if (item.actor.address != value)
                return false;
        for (const auto& candidate : snapshot.mortar.candidates)
            if (candidate.actor_addr != value)
                return false;
        for (const auto& marker : snapshot.markers)
            if (marker.actor != value)
                return false;
        for (const auto& trail : snapshot.trails)
            if (trail.points[0].X != value || trail.count != 2)
                return false;
        return true;
    }

    struct TestContext
    {
        FrameExchange exchange;
        HANDLE phase;
        HANDLE resume;
        HANDLE done;
        DWORD producer_thread = 0;
        bool inputs_consistent = true;
        bool stopped = false;
    };

    DWORD WINAPI produce(void* data)
    {
        auto& test = *static_cast<TestContext*>(data);
        test.producer_thread = GetCurrentThreadId();
        game::Snapshot next;
        next.players.resize(8); // Deliberately pause with an incomplete private scan.
        SetEvent(test.phase);
        if (WaitForSingleObject(test.resume, 3000) != WAIT_OBJECT_0)
            return 1;
        fill(next, 2);
        test.exchange.publish(next);
        SetEvent(test.phase);
        if (WaitForSingleObject(test.resume, 3000) != WAIT_OBJECT_0)
            return 2;
        next.players.clear();
        AcquireSRWLockExclusive(&test.exchange.mutex);
        SetEvent(test.phase);
        const DWORD resumed = WaitForSingleObject(test.resume, 3000);
        ReleaseSRWLockExclusive(&test.exchange.mutex);
        if (resumed != WAIT_OBJECT_0)
            return 3;
        Config settings;
        bool visible;
        for (unsigned value = 100; value < 3100; ++value)
        {
            if (!test.exchange.read_inputs(settings, visible))
                return 4;
            const auto input = static_cast<unsigned>(settings.aimbot.fov);
            test.inputs_consistent &= settings.aimbot.bone == input % 4 && visible == ((input & 1) != 0);
            fill(next, value);
            if (!test.exchange.publish(next))
                return 5;
        }
        SetEvent(test.done);
        const auto deadline = GetTickCount64() + 3000;
        while (test.exchange.read_inputs(settings, visible))
            if (GetTickCount64() >= deadline)
                return 6;
        test.stopped = !test.exchange.publish(next);
        return 0;
    }
} // namespace

int test_frame_exchange()
{
    int failures = 0;
    const auto check = [&](bool passed, const char* name)
    {
        printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
        failures += !passed;
    };
    TestContext test{};
    game::Snapshot next, displayed;
    Config settings;
    settings.aimbot.fov = 80;
    settings.aimbot.bone = 0;
    check(!test.exchange.refresh(displayed, settings, false) && !displayed.valid, "no snapshot displayed before first publication");
    fill(next, 1);
    test.exchange.publish(next);
    check(test.exchange.refresh(displayed, settings, false) && consistent(displayed), "complete initial snapshot handed to renderer");
    check(!test.exchange.refresh(displayed, settings, false) && consistent(displayed), "no new publication retains the last complete frame");
    test.phase = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    test.resume = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    test.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE worker = test.phase && test.resume && test.done ? CreateThread(nullptr, 0, produce, &test, 0, nullptr) : nullptr;
    check(worker != nullptr, "start separate snapshot producer");
    if (worker)
    {
        check(WaitForSingleObject(test.phase, 3000) == WAIT_OBJECT_0, "producer paused during an incomplete scan");
        test.exchange.refresh(displayed, settings, false);
        check(consistent(displayed) && displayed.camera.location.X == 1, "render copy survives an incomplete producer scan");
        SetEvent(test.resume);
        check(WaitForSingleObject(test.phase, 3000) == WAIT_OBJECT_0, "producer publishes finished scan");
        check(test.exchange.refresh(displayed, settings, false) && consistent(displayed) && displayed.camera.location.X == 2, "new frame replaces old frame only after publication");
        SetEvent(test.resume);
        check(WaitForSingleObject(test.phase, 3000) == WAIT_OBJECT_0, "producer holds the handoff lock");
        LARGE_INTEGER start, finish, frequency;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        const bool changed = test.exchange.refresh(displayed, settings, false);
        QueryPerformanceCounter(&finish);
        check(!changed && consistent(displayed) && displayed.camera.location.X == 2 && 1000.0 * (finish.QuadPart - start.QuadPart) / frequency.QuadPart < 20, "busy handoff keeps the render copy without blocking");
        SetEvent(test.resume);
        unsigned reads = 0, input = 80;
        bool snapshots_consistent = true;
        const auto deadline = GetTickCount64() + 5000;
        while (WaitForSingleObject(test.done, 0) != WAIT_OBJECT_0 && GetTickCount64() < deadline)
        {
            input = 80 + (input + 1) % 600;
            settings.aimbot.fov = static_cast<float>(input);
            settings.aimbot.bone = input % 4;
            if (test.exchange.refresh(displayed, settings, (input & 1) != 0))
            {
                ++reads;
                snapshots_consistent &= consistent(displayed);
            }
        }
        test.exchange.refresh(displayed, settings, (input & 1) != 0);
        check(WaitForSingleObject(test.done, 0) == WAIT_OBJECT_0 && snapshots_consistent && consistent(displayed) && displayed.camera.location.X == 3099, "3000 publications preserve complete player, world and mortar data");
        printf("Concurrent snapshot reads: %u\n", reads);
        test.exchange.stop();
        if (WaitForSingleObject(worker, 5000) != WAIT_OBJECT_0)
        {
            printf("FAIL: snapshot producer did not join\n");
            ExitProcess(1);
        }
        DWORD exit_code = 1;
        GetExitCodeThread(worker, &exit_code);
        check(exit_code == 0 && test.stopped, "stop joins producer and rejects further publication");
        check(test.inputs_consistent && test.producer_thread != GetCurrentThreadId(), "producer receives consistent settings and menu state on another thread");
        CloseHandle(worker);
    }
    for (HANDLE event : {test.phase, test.resume, test.done})
        if (event)
            CloseHandle(event);
    FrameExchange transition;
    fill(next, 7);
    transition.publish(next);
    transition.refresh(displayed, settings, false);
    next = {};
    transition.publish(next);
    check(transition.refresh(displayed, settings, false) && !displayed.valid && displayed.players.empty(), "completed invalid-world update clears the previous scene");
    return failures;
}
