#include "../src/config.h"
#include "../src/game.h"
#include "../src/hook_process_event.h"

namespace
{
    unsigned lookups, events;

    void* __fastcall find_object(void*, void*, const FString*, bool)
    {
        ++lookups;
        return nullptr;
    }

    void __fastcall process_event(void*, void*, void*)
    {
        ++events;
    }

    struct Object
    {
        alignas(8) unsigned char bytes[0x800]{};

        Object()
        {
            put(offsets::UObject::VTable, address());
            put(offsets::UObject::ClassPrivate, address());
        }

        std::uintptr_t address() const
        {
            return reinterpret_cast<std::uintptr_t>(bytes);
        }

        template <typename T>
        void put(std::size_t offset, T value)
        {
            memcpy(bytes + offset, &value, sizeof(value));
        }
    };
} // namespace

int test_transitions()
{
    int failures = 0;
    const auto check = [&](bool passed, const char* message)
    {
        printf("%s: %s\n", passed ? "PASS" : "FAIL", message);
        failures += !passed;
    };
    const auto saved_base = offsets::base, saved_world = offsets::UWorldPtr;
    const auto saved_find = offsets::Functions::StaticFindObject, saved_event = offsets::Functions::ProcessEvent;
    const auto saved_ticks = frame_ticks;
    std::uintptr_t world_pointer = 0;
    offsets::base = 1;
    offsets::UWorldPtr = reinterpret_cast<std::uintptr_t>(&world_pointer) - 1;
    offsets::Functions::StaticFindObject = reinterpret_cast<std::uintptr_t>(&find_object) - 1;
    offsets::Functions::ProcessEvent = reinterpret_cast<std::uintptr_t>(&process_event) - 1;
    frame_ticks = 2000;
    Object world, level, instance, state, player, controller, pawn, camera, player_state, mesh;
    world.put(offsets::World::PersistentLevel, level.address());
    world.put(offsets::World::OwningGameInstance, instance.address());
    world.put(offsets::World::GameState, state.address());
    auto player_address = player.address();
    TArray<std::uintptr_t> players;
    players.Data = &player_address;
    players.Count = players.Max = 1;
    instance.put(offsets::UGameInstance::LocalPlayers, players);
    player.put(offsets::UPlayer::PlayerController, controller.address());
    controller.put(offsets::APlayerController::PlayerCameraManager, camera.address());
    pawn.put(offsets::ACharacter::Mesh, mesh.address());
    game::Snapshot snapshot;
    const Config settings;
    game::init();
    const auto tick = [&]()
    {
        snapshot.valid = true;
        snapshot.players.resize(1);
        snapshot.vehicles.resize(1);
        snapshot.dropped_items.resize(1);
        lookups = events = 0;
        game::tick(snapshot, settings, false);
        return !snapshot.valid && snapshot.players.empty() && snapshot.vehicles.empty() && snapshot.dropped_items.empty();
    };
    check(tick() && lookups == 0 && events == 0, "missing world clears snapshot without native calls");
    world_pointer = world.address();
    check(tick() && lookups == 0 && events == 0, "frontend controller without a pawn never enters native initialization");
    controller.put(offsets::APlayerController_Extra::ControllerPawn, pawn.address());
    check(tick() && lookups == 0 && events == 0, "menu pawn without PlayerState never enters native initialization");
    pawn.put(offsets::APawn::PlayerState, player_state.address());
    player_state.put(offsets::APlayerState::PawnPrivate, controller.address());
    check(tick() && lookups == 0 && events == 0, "stale PlayerState ownership is rejected before native calls");
    player_state.put(offsets::APlayerState::PawnPrivate, pawn.address());
    check(tick() && lookups > 0 && events == 0, "ready gameplay context reaches function resolution");
    check(tick() && lookups == 0 && events == 0, "unchanged match retains its function-resolution state");
    world_pointer = 0;
    check(tick() && lookups == 0 && events == 0, "leaving match clears context without calling into the old world");
    world_pointer = world.address();
    check(tick() && lookups > 0 && events == 0, "rejoining reacquires functions even when world address is reused");
    Object next_world;
    next_world.put(offsets::World::PersistentLevel, level.address());
    next_world.put(offsets::World::OwningGameInstance, instance.address());
    next_world.put(offsets::World::GameState, state.address());
    world_pointer = next_world.address();
    check(tick() && lookups > 0 && events == 0, "direct world replacement resets function-resolution state");
    controller.put(offsets::APlayerController_Extra::ControllerPawn, std::uintptr_t(0));
    check(tick() && lookups == 0 && events == 0, "unpossession stops native calls while world memory remains readable");
    controller.put(offsets::APlayerController_Extra::ControllerPawn, pawn.address());
    check(tick() && lookups > 0 && events == 0, "respawn rebuilds match state");
    for (auto* object : {&next_world, &level, &controller, &pawn, &camera, &player_state, &mesh})
    {
        object->put(offsets::UObject::ObjectFlags, std::uint32_t(0x8000));
        check(tick() && lookups == 0 && events == 0, "destroying context object prevents native work");
        object->put(offsets::UObject::ObjectFlags, std::uint32_t(0));
    }
    game::shutdown();

    Object target, function;
    int params = 0;
    events = 0;
    check(engine::call_process_event(target.bytes, function.bytes, &params) && events == 1, "live objects still reach native ProcessEvent");
    for (const std::uint32_t flag : {0x200u, 0x400u, 0x1000u, 0x8000u, 0x10000u, 0x40000000u})
    {
        target.put(offsets::UObject::ObjectFlags, flag);
        check(!engine::call_process_event(target.bytes, function.bytes, &params) && events == 1, "unready or destroyed object is rejected despite readable memory");
        target.put(offsets::UObject::ObjectFlags, std::uint32_t(0));
        function.put(offsets::UObject::ObjectFlags, flag);
        check(!engine::call_process_event(target.bytes, function.bytes, &params) && events == 1, "unready or destroyed UFunction is rejected");
        function.put(offsets::UObject::ObjectFlags, std::uint32_t(0));
    }
    target.put(offsets::UObject::ClassPrivate, std::uintptr_t(0));
    check(!engine::call_process_event(target.bytes, function.bytes, &params) && events == 1, "readable non-object memory is rejected before native call");
    offsets::base = saved_base;
    offsets::UWorldPtr = saved_world;
    offsets::Functions::StaticFindObject = saved_find;
    offsets::Functions::ProcessEvent = saved_event;
    frame_ticks = saved_ticks;
    return failures;
}
