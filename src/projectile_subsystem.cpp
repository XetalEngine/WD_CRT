#include "stdafx.h"
#include "projectile_subsystem.h"
#include "classes.h"
#include "engine_funcs.h"
#include "offsets.h"
#include "hook_process_event.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace wdgs::projectile_subsystem
{
    namespace
    {

        constexpr std::uint32_t kScanBudget = 256;
        constexpr std::uint64_t kRescanDelayMs = 1000;

        std::uintptr_t g_subsystem = 0;
        std::uintptr_t g_subsystem_class = 0;
        std::uintptr_t g_projectile_class = 0;
        std::uint32_t g_scan_cursor = 0;
        std::uint32_t g_scan_count = 0;
        std::uint64_t g_next_scan_ms = 0;
        std::unordered_map<std::uintptr_t, bool> g_class_matches;

        bool finite_range(std::uintptr_t base, std::size_t bytes)
        {
            return base != 0 && bytes != 0 && bytes <= 0x1000000 &&
                   is_valid_ptr(reinterpret_cast<const void*>(base), bytes);
        }

        bool class_is_projectile_subsystem(std::uintptr_t class_ptr)
        {
            if (!class_ptr || !is_valid_ptr(reinterpret_cast<const void*>(class_ptr)))
                return false;
            const auto cached = g_class_matches.find(class_ptr);
            if (cached != g_class_matches.end())
                return cached->second;

            const std::string name = FName::ToString(static_cast<std::uint32_t>(read<std::int32_t>(class_ptr + offsets::UObject::NamePrivate)));
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            const bool match = lower.find("projectilesubsystem") != std::string::npos;
            g_class_matches.emplace(class_ptr, match);
            return match;
        }

        std::uintptr_t resolve_projectile_class()
        {
            if (g_projectile_class && is_valid_ptr(reinterpret_cast<const void*>(g_projectile_class)))
                return g_projectile_class;
            constexpr const wchar_t* paths[] = {
                L"/Script/WDGame.WDProjectileSubsystem",
                L"/Script/WDGame.FWDProjectileSubsystem",
            };
            for (const wchar_t* path : paths)
            {
                void* found = engine::static_find_object(nullptr, nullptr, path);
                if (is_valid_ptr(found))
                {
                    g_projectile_class = reinterpret_cast<std::uintptr_t>(found);
                    return g_projectile_class;
                }
            }
            return 0;
        }

        struct ObjectArrayView
        {
            std::uintptr_t array = 0;
            std::uintptr_t chunks = 0;
            std::uint32_t count = 0;
            bool indirect = false;
        };

        ObjectArrayView object_array_view()
        {
            ObjectArrayView view{};
            const std::uintptr_t global = offsets::base + offsets::GObjects;
            if (!global)
                return view;
            auto sane = [](std::uint32_t count, std::uintptr_t chunks)
            {
                return count != 0 && count <= 0x4000000u &&
                       is_valid_ptr(reinterpret_cast<const void*>(chunks));
            };
            const std::uint32_t direct_count = read<std::uint32_t>(global + offsets::ObjectArray::NumElements);
            const std::uintptr_t direct_chunks = read<std::uintptr_t>(global + offsets::ObjectArray::Objects);
            if (sane(direct_count, direct_chunks))
                return {global, direct_chunks, direct_count, false};

            const std::uintptr_t indirect = read<std::uintptr_t>(global);
            if (!is_valid_ptr(reinterpret_cast<const void*>(indirect)))
                return view;
            const std::uint32_t indirect_count = read<std::uint32_t>(indirect + offsets::ObjectArray::NumElements);
            const std::uintptr_t indirect_chunks = read<std::uintptr_t>(indirect + offsets::ObjectArray::Objects);
            if (!sane(indirect_count, indirect_chunks))
                return view;
            return {indirect, indirect_chunks, indirect_count, true};
        }

        UObject* object_at(const ObjectArrayView& view, std::uint32_t index)
        {
            if (!view.array || index >= view.count)
                return nullptr;
            const std::uintptr_t chunk = read<std::uintptr_t>(view.chunks + (index >> 16u) * sizeof(std::uintptr_t));
            if (!is_valid_ptr(reinterpret_cast<const void*>(chunk)))
                return nullptr;
            const std::uintptr_t slot = chunk + static_cast<std::uint16_t>(index) * sizeof(FUObjectItem);
            const std::uintptr_t object = read<std::uintptr_t>(slot + 0x08);
            return is_valid_ptr(reinterpret_cast<const void*>(object))
                       ? reinterpret_cast<UObject*>(object)
                       : nullptr;
        }

        std::uintptr_t find_from_world()
        {
            const std::uintptr_t class_ptr = resolve_projectile_class();
            if (!class_ptr || !offsets::base || !offsets::UWorldPtr)
                return 0;
            const std::uintptr_t world = read<std::uintptr_t>(offsets::base + offsets::UWorldPtr);
            if (!is_valid_ptr(reinterpret_cast<const void*>(world)))
                return 0;
            void* result = engine_funcs::get_world_subsystem(reinterpret_cast<void*>(world), reinterpret_cast<void*>(class_ptr));
            if (!is_valid_ptr(result))
                return 0;
            g_subsystem_class = class_ptr;
            return reinterpret_cast<std::uintptr_t>(result);
        }

        std::uintptr_t find_subsystem(Snapshot& out)
        {
            const std::uint64_t now = frame_ticks;
            if (g_subsystem && now < g_next_scan_ms)
            {
                const std::uintptr_t current_class = read<std::uintptr_t>(g_subsystem + offsets::UObject::ClassPrivate);
                if (current_class == g_subsystem_class && is_valid_ptr(reinterpret_cast<const void*>(g_subsystem)))
                    return g_subsystem;
                g_subsystem = 0;
                g_subsystem_class = 0;
            }
            if (!g_subsystem && now < g_next_scan_ms)
                return 0;

            if (const std::uintptr_t direct = find_from_world())
            {
                g_subsystem = direct;
                g_next_scan_ms = now + kRescanDelayMs;
                return g_subsystem;
            }

            const ObjectArrayView objects = object_array_view();
            out.object_array = objects.array;
            out.indirect_object_array = objects.indirect;
            out.object_count = objects.count;
            if (objects.count == 0 || objects.count > 0x4000000u)
                return 0;
            if (g_scan_count != objects.count)
            {
                g_scan_count = objects.count;
                if (g_scan_cursor >= objects.count)
                    g_scan_cursor = 0;
            }
            const std::uint32_t end = (std::min)(objects.count, g_scan_cursor + kScanBudget);
            for (std::uint32_t i = g_scan_cursor; i < end; ++i)
            {
                UObject* object = object_at(objects, i);
                if (!object)
                    continue;
                const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(object);
                const std::uintptr_t class_ptr = read<std::uintptr_t>(address + offsets::UObject::ClassPrivate);
                if (!class_is_projectile_subsystem(class_ptr))
                    continue;
                g_subsystem = address;
                g_subsystem_class = class_ptr;
                g_scan_cursor = 0;
                g_next_scan_ms = now + kRescanDelayMs;
                return g_subsystem;
            }
            g_scan_cursor = end;
            if (g_scan_cursor >= objects.count)
            {
                g_scan_cursor = 0;
                g_next_scan_ms = now + kRescanDelayMs;
            }
            return 0;
        }

        bool read_bits(std::uintptr_t subsystem, std::uint32_t count, std::array<std::uint32_t, (max_allocated + 31u) / 32u>& bits)
        {
            const std::uint32_t words = (count + 31u) / 32u;
            if (words <= 4u)
                return read_mem(subsystem + inline_bits_offset, bits.data(), words * sizeof(std::uint32_t));
            const std::uintptr_t heap_bits = read<std::uintptr_t>(subsystem + heap_bits_offset);
            return finite_range(heap_bits, static_cast<std::size_t>(words) * sizeof(std::uint32_t)) &&
                   read_mem(heap_bits, bits.data(), words * sizeof(std::uint32_t));
        }

    } // namespace

    bool acquire(Snapshot& out)
    {
        out = {};
        const std::uintptr_t subsystem = find_subsystem(out);
        out.subsystem = g_subsystem;
        out.subsystem_class = g_subsystem_class;
        if (!subsystem)
            return false;
        out.pool = read<std::uintptr_t>(subsystem + pool_offset);
        out.allocated = read<std::uint32_t>(subsystem + allocated_offset);
        if (!out.pool || out.allocated == 0 || out.allocated > max_allocated)
            return false;
        const std::uintptr_t last = out.pool + static_cast<std::uintptr_t>(out.allocated - 1u) * instance_stride;
        if (!finite_range(out.pool, sizeof(std::uintptr_t)) || !finite_range(last, instance_read_end))
            return false;
        if (!read_bits(subsystem, out.allocated, out.active_bits))
            return false;
        return true;
    }

    bool read_instance(const Snapshot& snapshot, std::uint32_t slot, Instance& out)
    {
        out = {};
        if (!snapshot.valid() || !snapshot.active(slot))
            return false;
        out.slot = slot;
        out.address = snapshot.pool + static_cast<std::uintptr_t>(slot) * instance_stride;
        if (!finite_range(out.address, instance_read_end))
            return false;
        out.location = read<FVector>(out.address + location_offset);
        out.velocity = read<FVector>(out.address + velocity_offset);
        out.landed = read<bool>(out.address + landed_offset);
        out.owner_internal_index = read<std::uint32_t>(out.address + owner_internal_index_offset);
        out.weapon_internal_index = read<std::uint32_t>(out.address + weapon_internal_index_offset);
        out.state = read<std::uint8_t>(out.address + state_offset);
        out.external_movement = read<std::uint8_t>(out.address + external_movement_offset);
        out.flight_time = read<double>(out.address + flight_time_offset);
        out.data = read<std::uintptr_t>(out.address + data_offset);
        if (is_valid_ptr(reinterpret_cast<const void*>(out.data)))
            out.data_name = read<FNameValue>(out.data + offsets::UObject::NamePrivate);
        return true;
    }

    void reset()
    {
        g_subsystem = 0;
        g_subsystem_class = 0;
        g_projectile_class = 0;
        g_scan_cursor = 0;
        g_scan_count = 0;
        g_next_scan_ms = 0;
        g_class_matches.clear();
    }

#ifdef WD_TEST
    void test_subsystem(std::uintptr_t subsystem)
    {
        g_subsystem = subsystem;
        g_subsystem_class = read<std::uintptr_t>(subsystem + offsets::UObject::ClassPrivate);
        g_next_scan_ms = frame_ticks + kRescanDelayMs;
    }
#endif
} // namespace wdgs::projectile_subsystem
