#pragma once
#include "stdafx.h"
#include "offsets.h"
#include "classes.h"

namespace engine
{
    using ProcessEventFn = void(__fastcall*)(void* object, void* function, void* params);
    using StaticFindObjectFn = void*(__fastcall*)(void* object_class, void* in_outer, const FString* name, bool exact_class);
    using FNameToStringFn = void(__fastcall*)(std::uintptr_t name, void* out_string, std::int64_t a3, std::int64_t a4);
    using FreeObjectNameFn = void(__fastcall*)(std::uintptr_t ptr);

    inline bool is_live_object(const void* object)
    {
        struct Header
        {
            std::uintptr_t vtable;
            std::uint32_t flags;
            std::int32_t index;
            std::uintptr_t object_class;
        } header{};
        // EObjectFlags: initialization/load, BeginDestroyed, FinishDestroyed, MirroredGarbage.
        constexpr std::uint32_t unavailable = 0x00000200 | 0x00000400 | 0x00001000 | 0x00008000 | 0x00010000 | 0x40000000;
        return read_mem(reinterpret_cast<std::uintptr_t>(object), &header, sizeof(header)) && header.index >= 0 && !(header.flags & unavailable) && is_userland_ptr(header.vtable) && is_userland_ptr(header.object_class);
    }

    inline ProcessEventFn get_process_event()
    {
        if (!offsets::base || !offsets::Functions::ProcessEvent)
            return nullptr;
        const auto address = offsets::base + offsets::Functions::ProcessEvent;
        return is_valid_ptr(reinterpret_cast<const void*>(address)) ? reinterpret_cast<ProcessEventFn>(address) : nullptr;
    }

    inline bool call_process_event(void* object, void* function, void* params)
    {
        auto process_event = get_process_event();
        // Readable memory can belong to an object that is already being destroyed.
        if (!process_event || !params || !is_live_object(object) || !is_live_object(function))
            return false;
        process_event(object, function, params);
        return true;
    }

    inline StaticFindObjectFn get_static_find_object()
    {
        if (!offsets::base || !offsets::Functions::StaticFindObject)
            return nullptr;
        const auto address = offsets::base + offsets::Functions::StaticFindObject;
        return is_valid_ptr(reinterpret_cast<const void*>(address)) ? reinterpret_cast<StaticFindObjectFn>(address) : nullptr;
    }

    inline void* static_find_object(void* object_class, void* in_outer, const wchar_t* name, bool exact_class = false)
    {
        auto find_object = get_static_find_object();
        if (!find_object || !name)
            return nullptr;
        const FString lookup_name(name); // Count == Max == wcslen(name), without NUL.
        void* result = find_object(object_class, in_outer, &lookup_name, exact_class);
        return is_valid_ptr(result) ? result : nullptr;
    }

    inline std::uintptr_t get_gengine()
    {
        return read<std::uintptr_t>(offsets::base + offsets::Globals::GEngine);
    }
} // namespace engine
