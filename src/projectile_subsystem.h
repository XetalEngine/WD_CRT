#pragma once

#include "structs.h"
#include <array>
#include <cstdint>

namespace wdgs::projectile_subsystem
{

    inline constexpr std::uintptr_t pool_offset = 0x108;
    inline constexpr std::uintptr_t allocated_offset = 0x110;
    inline constexpr std::uintptr_t inline_bits_offset = 0x118;
    inline constexpr std::uintptr_t heap_bits_offset = 0x128;
    inline constexpr std::uintptr_t instance_stride = 0x320;
    inline constexpr std::uintptr_t location_offset = 0x08;
    inline constexpr std::uintptr_t velocity_offset = 0x20;
    inline constexpr std::uintptr_t landed_offset = 0x118;
    inline constexpr std::uintptr_t owner_internal_index_offset = 0x19C;
    inline constexpr std::uintptr_t weapon_internal_index_offset = 0x1A0;
    inline constexpr std::uintptr_t data_offset = 0xE8;
    inline constexpr std::uintptr_t state_offset = 0xC8;
    inline constexpr std::uintptr_t external_movement_offset = 0x128;
    inline constexpr std::uintptr_t flight_time_offset = 0x290;
    inline constexpr std::uint32_t max_allocated = 4096;
    inline constexpr std::uintptr_t instance_read_end =
        weapon_internal_index_offset + sizeof(std::uint32_t);

    struct Snapshot
    {
        std::uintptr_t subsystem = 0;
        std::uintptr_t subsystem_class = 0;
        std::uintptr_t pool = 0;
        std::uintptr_t object_array = 0;
        bool indirect_object_array = false;
        std::uint32_t object_count = 0;
        std::uint32_t allocated = 0;
        std::array<std::uint32_t, (max_allocated + 31u) / 32u> active_bits{};

        bool valid() const
        {
            return subsystem != 0 && pool != 0 && allocated != 0;
        }
        bool active(std::uint32_t slot) const
        {
            return slot < allocated && ((active_bits[slot >> 5u] >> (slot & 31u)) & 1u) != 0u;
        }
    };

    struct Instance
    {
        std::uint32_t slot = 0;
        std::uintptr_t address = 0;
        std::uintptr_t data = 0;
        FNameValue data_name{};
        FVector location{};
        FVector velocity{};
        std::uint32_t owner_internal_index = 0;
        std::uint32_t weapon_internal_index = 0;
        bool landed = false;
        std::uint8_t state = 0;
        std::uint8_t external_movement = 0;
        double flight_time = 0.0;
    };

    // Reads the cached WDProjectileSubsystem pool and its active bitset. Discovery
    // uses StaticFindObject/GetWorldSubsystem first and a bounded GObjects scan only
    // when the world subsystem is not available.
    bool acquire(Snapshot& out);
    bool read_instance(const Snapshot& snapshot, std::uint32_t slot, Instance& out);
    void reset();
#ifdef WD_TEST
    void test_subsystem(std::uintptr_t subsystem);
#endif

} // namespace wdgs::projectile_subsystem
