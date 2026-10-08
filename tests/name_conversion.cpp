#include "../src/classes.h"
#include "../src/engine_funcs.h"

namespace
{
    unsigned calls;

    void __fastcall convert(void*, void*, void* data)
    {
        struct Params
        {
            FNameValue name;
            FString result;
        };
        ++calls;
        static_cast<Params*>(data)->result = FString(L"WDStationaryVehicle");
    }
} // namespace

int test_name_conversion()
{
    int failures = 0;
    const auto check = [&](bool passed, const char* message)
    {
        printf("%s: %s\n", passed ? "PASS" : "FAIL", message);
        failures += !passed;
    };
    struct Pool
    {
        std::uint64_t lock = 0;
        std::uint32_t current_block = 1, cursor = 64;
        std::uintptr_t blocks[3]{};
    } pool;
    unsigned char block[128]{};
    pool.blocks[1] = reinterpret_cast<std::uintptr_t>(block);
    const std::uint16_t header = 19 << 6;
    memcpy(block + 16 + 8, &header, sizeof(header));
    memcpy(block + 16 + 12, "WDStationaryVehicle", 19);
    struct Object
    {
        std::uintptr_t vtable;
        std::uint32_t flags = 0;
        std::int32_t index = 0;
        std::uintptr_t object_class;
    } library{}, function{};
    library.vtable = library.object_class = reinterpret_cast<std::uintptr_t>(&library);
    function.vtable = function.object_class = reinterpret_cast<std::uintptr_t>(&function);
    const auto saved_base = offsets::base, saved_names = offsets::GNames, saved_event = offsets::Functions::ProcessEvent;
    offsets::base = 1;
    offsets::GNames = reinterpret_cast<std::uintptr_t>(&pool) - 1;
    offsets::Functions::ProcessEvent = reinterpret_cast<std::uintptr_t>(&convert) - 1;
    engine_funcs::test_name_conversion(&library, &function);

    calls = 0;
    auto name = engine_funcs::conv_name_to_string({0x96C0A300, 0x7FF8});
    check(!name.IsValid() && calls == 0, "exact mid-match crash name is rejected before native conversion");
    name = engine_funcs::conv_name_to_string({0x20002, 0});
    check(!name.IsValid() && calls == 0, "missing name-pool block is rejected before native conversion");
    name = engine_funcs::conv_name_to_string({0x10002, 0});
    check(name.IsValid() && name.ToWString(64) == L"WDStationaryVehicle" && calls == 1, "valid name still reaches native conversion");
    const std::uint16_t empty_wide = 1;
    memcpy(block + 16 + 8, &empty_wide, sizeof(empty_wide));
    name = engine_funcs::conv_name_to_string({0x10002, 0});
    check(!name.IsValid() && calls == 1, "empty name entry is rejected before native conversion");

    auto* pages = static_cast<unsigned char*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(pages != nullptr, "allocate name-pool boundary regression fixture");
    if (pages)
    {
        const std::uint16_t boundary_header = 19 << 6;
        // Entry/header are readable; the string begins at a protected page.
        pool.blocks[1] = reinterpret_cast<std::uintptr_t>(pages + 4096 - 28);
        memcpy(pages + 4092, &boundary_header, sizeof(boundary_header));
        DWORD old;
        VirtualProtect(pages + 4096, 4096, PAGE_NOACCESS, &old);
        name = engine_funcs::conv_name_to_string({0x10002, 0});
        check(!name.IsValid() && calls == 1, "unreadable name payload is rejected before native conversion");
        VirtualFree(pages, 0, MEM_RELEASE);
    }
    engine_funcs::test_name_conversion(nullptr, nullptr);
    offsets::base = saved_base;
    offsets::GNames = saved_names;
    offsets::Functions::ProcessEvent = saved_event;
    return failures;
}
