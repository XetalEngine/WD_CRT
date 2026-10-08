#include "game_actions.h"
#include "extras.h"
#include "hook_process_event.h"
#include "visual_math.h"

namespace
{
    struct Request
    {
        bool build = false, join = false, afk = false, aiming = false, menu = false;
        ULONGLONG now = 0;
        HHOOK cleanup = nullptr;
    } request;
    SRWLOCK mailbox = SRWLOCK_INIT;
    bool pending = false;
    bool was_enabled = false;
    HHOOK hook = nullptr;
    DWORD message_thread = 0;
    ULONGLONG next_publish = 0, next_install = 0;
    // Only the game's message thread owns everything below.
    std::uintptr_t active_world = 0, active_pawn = 0;
    ULONGLONG next_resolve = 0, next_join = 0, next_afk = 0;
    int join_key = 0x10000, afk_side = 1;
    bool commit_next = false;
    FNameValue manticore{};
    void *session_class = nullptr, *state_class = nullptr, *reserve_fn = nullptr, *commit_fn = nullptr;
    void *manager_class = nullptr, *tool_class = nullptr, *decal_class = nullptr;
    void *component_fn = nullptr, *eyes_fn = nullptr, *location_fn = nullptr, *trace_fn = nullptr;
    void *get_rotation_fn = nullptr, *set_rotation_fn = nullptr;
    struct Spot
    {
        std::uintptr_t decal = 0;
        FNameValue name{};
        FVector original{}, last{};
        unsigned seen = 0;
    } spots[24];
    unsigned pass = 0;

    bool live(std::uintptr_t p)
    {
        return engine::is_live_object(reinterpret_cast<void*>(p));
    }
    bool call(std::uintptr_t p, void* fn, void* data)
    {
        return engine::call_process_event(reinterpret_cast<void*>(p), fn, data);
    }

    bool same(const Spot& spot)
    {
        if (!spot.decal || !spot.name.ComparisonIndex || !extras::derives(spot.decal, decal_class))
            return false;
        const auto name = read<FNameValue>(spot.decal + offsets::UObject::NamePrivate);
        return name.ComparisonIndex == spot.name.ComparisonIndex && name.Number == spot.name.Number;
    }

    bool move(std::uintptr_t decal, const FVector& location)
    {
        struct
        {
            FVector position;
            unsigned char rest[0x200];
        } params{};
        params.position = location;
        return call(decal, location_fn, &params);
    }

    void restore()
    {
        const bool world_valid = active_world && read<std::uintptr_t>(offsets::base + offsets::UWorldPtr) == active_world && live(active_world);
        for (auto& spot : spots)
        {
            if (world_valid && same(spot))
            {
                const auto current = read<FVector>(spot.decal + 0x230);
                if (visual_math::finite(current) && current.Distance(spot.last) <= 2)
                    move(spot.decal, spot.original);
            }
            spot = {};
        }
    }

    void resolve(const Request& input)
    {
        if (input.now < next_resolve)
            return;
        next_resolve = input.now + 5000;
        const auto find = [](void*& fn, const wchar_t* path)
        { if (!fn) fn = engine::static_find_object(nullptr, nullptr, path); };
        find(session_class, L"/Script/WDGame.WDGameStateSession");
        if (input.join)
        {
            find(state_class, L"/Script/WDGame.WDPlayerStateSession");
            find(reserve_fn, L"/Script/WDGame.WDPlayerStateSession:Server_ReserveFaction");
            find(commit_fn, L"/Script/WDGame.WDPlayerStateSession:Server_CommitFactionReservation");
            if (!manticore.ComparisonIndex)
            {
                auto library = engine::static_find_object(nullptr, nullptr, L"/Script/Engine.Default__KismetStringLibrary");
                auto fn = engine::static_find_object(nullptr, nullptr, L"/Script/Engine.KismetStringLibrary:Conv_StringToName");
                struct
                {
                    FString text;
                    FNameValue name;
                } params{FString(L"Meta.Alignment.Faction.Charlie"), {}};
                if (engine::call_process_event(library, fn, &params))
                    manticore = params.name;
            }
        }
        if (input.build)
        {
            find(manager_class, L"/Script/BuildablesRuntime.WDPawnBuildableManager");
            find(tool_class, L"/Script/BuildablesRuntime.WDItemExtension_BuildBuildable");
            find(decal_class, L"/Script/Engine.DecalComponent");
            find(component_fn, L"/Script/Engine.Actor:GetComponentByClass");
            find(eyes_fn, L"/Script/Engine.Actor:GetActorEyesViewPoint");
            find(location_fn, L"/Script/Engine.SceneComponent:K2_SetWorldLocation");
            find(trace_fn, L"/Script/Engine.KismetSystemLibrary:LineTraceSingle");
        }
        if (input.afk)
        {
            find(get_rotation_fn, L"/Script/Engine.Controller:GetControlRotation");
            find(set_rotation_fn, L"/Script/Engine.Controller:SetControlRotation");
        }
    }

    struct Sparse
    {
        std::uintptr_t data;
        int count, capacity;
        std::uint32_t inline_bits[4];
        std::uintptr_t bits;
        int bit_count, bit_capacity, first_free, free_count;
    };
    static_assert(sizeof(Sparse) == 0x38);

    bool read_sparse(std::uintptr_t address, int stride, Sparse& map)
    {
        return read_mem(address, &map, sizeof(map)) && map.count >= 0 && map.count <= 64 && map.capacity >= map.count && map.capacity <= 4096 && map.bit_count >= map.count && map.bit_count <= map.bit_capacity && map.bit_capacity <= 4096 && (!map.count || is_readable_ptr(reinterpret_cast<void*>(map.data), map.count * stride));
    }

    bool allocated(const Sparse& map, int index)
    {
        const auto bits = map.bits ? read<std::uint32_t>(map.bits + (index / 32) * 4) : map.inline_bits[index / 32];
        return ((bits >> (index & 31)) & 1) != 0;
    }

    std::uintptr_t component(std::uintptr_t actor, void* cls)
    {
        struct
        {
            void* cls;
            std::uintptr_t result;
            unsigned char pad[32];
        } params{cls};
        return cls && call(actor, component_fn, &params) && extras::derives(params.result, cls) ? params.result : 0;
    }

    bool hammer(std::uintptr_t item)
    {
        if (!live(item) || !tool_class)
            return false;
        if (component(item, tool_class))
            return true;
        Sparse extensions{};
        if (!read_sparse(item + 0x2D8, 0x18, extensions))
            return false;
        for (int i = 0; i < extensions.count; ++i)
            if (allocated(extensions, i) && extras::derives(read<std::uintptr_t>(extensions.data + i * 0x18 + 8), tool_class))
                return true;
        return false;
    }

    double surface(std::uintptr_t pawn, std::uintptr_t item, const FVector& eye, const FVector& direction)
    {
        // Verified UE LineTraceSingle parameter offsets from spot/Utils/Util.cpp.
        alignas(8) unsigned char params[0x188]{};
        const FVector end = eye + direction * 250.0;
        std::uintptr_t ignore[]{pawn, item};
        TArray<std::uintptr_t> actors;
        actors.Data = ignore;
        actors.Count = actors.Max = 2;
        memcpy(params, &pawn, 8);
        memcpy(params + 8, &eye, 24);
        memcpy(params + 0x20, &end, 24);
        params[0x38] = 1;
        params[0x158] = 1;
        memcpy(params + 0x40, &actors, sizeof(actors));
        if (!call(pawn, trace_fn, params) || !params[0x180])
            return -1;
        float fraction = 0;
        memcpy(&fraction, params + 0x5C, 4);
        return std::isfinite(fraction) && fraction >= 0 && fraction <= 1 ? fraction * 250.0 : -1;
    }

    void build(std::uintptr_t pawn)
    {
        if (!component_fn || !eyes_fn || !location_fn || !trace_fn || !live(pawn))
        {
            restore();
            return;
        }
        const auto inventory = read<std::uintptr_t>(pawn + 0x748);
        const auto item = live(inventory) ? read<std::uintptr_t>(inventory + 0x8C8) : 0;
        if (!hammer(item))
        {
            restore();
            return;
        }
        const auto manager = component(pawn, manager_class);
        Sparse map{};
        if (!manager || !read_sparse(manager + 0xE8, 0x90, map))
        {
            restore();
            return;
        }
        struct
        {
            FVector eye;
            FRotator rotation;
            unsigned char pad[64];
        } view{};
        if (!call(pawn, eyes_fn, &view) || !visual_math::finite(view.eye) || !std::isfinite(view.rotation.Pitch) || !std::isfinite(view.rotation.Yaw))
        {
            restore();
            return;
        }
        constexpr double rad = 0.017453292519943295;
        const double pitch = view.rotation.Pitch * rad, yaw = view.rotation.Yaw * rad;
        const FVector direction{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
        ++pass;
        bool traced = false;
        double hit = -1;
        for (int i = 0; i < map.count; ++i)
        {
            if (!allocated(map, i))
                continue;
            const auto decal = read<std::uintptr_t>(map.data + i * 0x90 + 0x18);
            if (!extras::derives(decal, decal_class))
                continue;
            const auto name = read<FNameValue>(decal + offsets::UObject::NamePrivate);
            const auto current = read<FVector>(decal + 0x230);
            if (!name.ComparisonIndex || !visual_math::finite(current))
                continue;
            Spot* found = nullptr;
            Spot* empty = nullptr;
            for (auto& spot : spots)
            {
                if (!spot.decal)
                    empty = &spot;
                if (spot.decal == decal && spot.name.ComparisonIndex == name.ComparisonIndex && spot.name.Number == name.Number)
                    found = &spot;
            }
            if (!found)
            {
                if (!empty || current.Distance(view.eye) > 1000)
                    continue;
                found = empty;
                *found = {decal, name, current, current};
            }
            found->seen = pass;
            if (current.Distance(found->last) > 2)
                found->original = current;
            const double away = found->original.Distance(view.eye);
            if (away > 1000)
            {
                move(decal, found->original);
                *found = {};
                continue;
            }
            double radius = read<double>(decal + 0x160);
            if (!std::isfinite(radius) || radius < 2 || radius > 300)
                radius = 25;
            if (!traced)
            {
                hit = surface(pawn, item, view.eye, direction);
                traced = true;
            }
            const double distance = hit > 0 ? std::max(hit - 1, radius + 5) : std::min(std::max(away, radius + 20), 150.0);
            const auto target = view.eye + direction * distance;
            if (target.Distance(current) <= 0.5 || move(decal, target))
                found->last = target;
        }
        for (auto& spot : spots)
            if (spot.decal && spot.seen != pass)
            {
                if (same(spot) && read<FVector>(spot.decal + 0x230).Distance(spot.last) <= 2)
                    move(spot.decal, spot.original);
                spot = {};
            }
    }

    void service(const Request& input)
    {
        if (!input.build && !input.join && !input.afk)
        {
            restore();
            next_afk = 0;
            return;
        }
        resolve(input);
        const auto world = read<std::uintptr_t>(offsets::base + offsets::UWorldPtr);
        if (!live(world))
        {
            restore();
            return;
        }
        if (world != active_world)
        {
            restore();
            active_world = world;
            active_pawn = 0;
            next_join = next_afk = 0;
            commit_next = false;
        }
        const auto state = read<std::uintptr_t>(world + offsets::World::GameState);
        if (!extras::derives(state, session_class))
        {
            restore();
            return;
        }
        const auto instance = read<std::uintptr_t>(world + offsets::World::OwningGameInstance);
        if (!live(instance))
        {
            restore();
            return;
        }
        const auto players = read<TArray<std::uintptr_t>>(instance + offsets::UGameInstance::LocalPlayers);
        std::uintptr_t player = 0;
        if (!players.TryGet(0, player, 8) || !live(player))
        {
            restore();
            return;
        }
        const auto controller = read<std::uintptr_t>(player + offsets::UPlayer::PlayerController);
        if (!live(controller))
        {
            restore();
            return;
        }
        const auto ps = read<std::uintptr_t>(controller + 0x2C0);
        if (input.join && input.now >= next_join && extras::derives(ps, state_class) && reserve_fn && commit_fn && manticore.ComparisonIndex)
        {
            next_join = input.now + 500;
            const auto faction = read<std::uintptr_t>(ps + offsets::AWDPlayerStateSession::FactionComponent);
            const auto current = live(faction) ? read<FNameValue>(faction + offsets::UWDFactionComponent::FactionTag) : FNameValue{};
            if (current.ComparisonIndex == manticore.ComparisonIndex)
                commit_next = false;
            else if (!commit_next)
            {
                struct
                {
                    FNameValue tag;
                    int key;
                    unsigned char pad[52];
                } params{manticore, ++join_key};
                commit_next = call(ps, reserve_fn, &params);
            }
            else
            {
                struct
                {
                    int key;
                    unsigned char pad[60];
                } params{join_key};
                call(ps, commit_fn, &params);
                commit_next = false;
            }
        }
        if (!input.join)
        {
            next_join = 0;
            commit_next = false;
        }
        const auto pawn = read<std::uintptr_t>(controller + offsets::APlayerController_Extra::ControllerPawn);
        if (!live(pawn) || !live(ps) || read<std::uintptr_t>(ps + offsets::APlayerState::PawnPrivate) != pawn || !live(read<std::uintptr_t>(pawn + offsets::ACharacter::Mesh)))
        {
            restore();
            return;
        }
        if (pawn != active_pawn)
        {
            restore();
            active_pawn = pawn;
            next_afk = 0;
        }
        const auto operator_component = read<std::uintptr_t>(pawn + offsets::AWDMoverCharacter::VehicleOperator);
        const bool seated = live(operator_component) && (read<std::uintptr_t>(operator_component + offsets::UWDVehicleOperatorComponent::CurrentSeat) || read<std::uintptr_t>(operator_component + offsets::UWDVehicleOperatorComponent::ReplicatedSeat));
        if (input.build && !input.menu && !seated)
            build(pawn);
        else
            restore();
        if (!input.afk || input.menu || seated || input.aiming)
        {
            next_afk = 0;
            return;
        }
        if (!next_afk)
            next_afk = input.now + 5000;
        if (input.now < next_afk)
            return;
        next_afk = input.now + 5000;
        FRotator rotation{};
        if (call(controller, get_rotation_fn, &rotation) && std::isfinite(rotation.Pitch) && std::isfinite(rotation.Yaw))
        {
            rotation.Yaw += 0.5 * afk_side;
            afk_side = -afk_side;
            call(controller, set_rotation_fn, &rotation);
        }
    }

    LRESULT CALLBACK on_message(int code, WPARAM wparam, LPARAM lparam)
    {
        static bool inside = false;
        if (inside)
            return CallNextHookEx(nullptr, code, wparam, lparam);
        Request input;
        bool run = false;
        if (code >= 0 && wparam == PM_REMOVE && TryAcquireSRWLockExclusive(&mailbox))
        {
            run = pending;
            if (run)
            {
                input = request;
                pending = false;
            }
            ReleaseSRWLockExclusive(&mailbox);
        }
        // ProcessEvent can pump messages itself. Nested dispatch never runs actions.
        if (run)
        {
            inside = true;
            service(input);
            if (input.cleanup)
                UnhookWindowsHookEx(input.cleanup);
            inside = false;
        }
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    BOOL CALLBACK find_window(HWND window, LPARAM param)
    {
        DWORD pid = 0;
        const DWORD thread = GetWindowThreadProcessId(window, &pid);
        char name[64]{};
        if (pid == GetCurrentProcessId() && IsWindowVisible(window) && GetClassNameA(window, name, sizeof(name)) && strcmp(name, "UnrealWindow") == 0)
        {
            *reinterpret_cast<DWORD*>(param) = thread;
            return FALSE;
        }
        return TRUE;
    }
} // namespace

void game_actions::update(const Config& settings, bool menu_visible)
{
    const bool enabled = settings.extra.build_x || settings.extra.auto_join || settings.extra.anti_afk;
    if (!hook && enabled && frame_ticks >= next_install)
    {
        next_install = frame_ticks + 5000;
        EnumWindows(find_window, reinterpret_cast<LPARAM>(&message_thread));
        if (message_thread)
            hook = SetWindowsHookExA(WH_GETMESSAGE, on_message, nullptr, message_thread);
        log(hook ? "game actions ready" : "game actions unavailable; retrying");
    }
    if (!enabled && !was_enabled)
        return;
    if (!hook || frame_ticks < next_publish)
        return;
    was_enabled = enabled;
    next_publish = frame_ticks + 16;
    const bool aiming = settings.aimbot.enabled && (GetAsyncKeyState(settings.aimbot.key) & 0x8000);
    AcquireSRWLockExclusive(&mailbox);
    const bool wake = !pending;
    request = {settings.extra.build_x, settings.extra.auto_join, settings.extra.anti_afk, aiming, menu_visible, frame_ticks};
    pending = true;
    ReleaseSRWLockExclusive(&mailbox);
    if (wake)
        PostThreadMessageW(message_thread, WM_NULL, 0, 0);
}

void game_actions::stop()
{
    if (!hook)
        return;
    AcquireSRWLockExclusive(&mailbox);
    request = {};
    request.cleanup = hook;
    hook = nullptr;
    pending = true;
    ReleaseSRWLockExclusive(&mailbox);
    PostThreadMessageW(message_thread, WM_NULL, 0, 0);
    // The DLL is pinned. A delayed cleanup callback remains valid after render stop.
}

#ifdef WD_TEST
void game_actions::test_service(const Config& settings, ULONGLONG now)
{
    service({settings.extra.build_x, settings.extra.auto_join, settings.extra.anti_afk, false, false, now});
}
#endif
