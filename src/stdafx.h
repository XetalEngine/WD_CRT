#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>
#include "xor_text.h"

inline int screen_width = 1920;
inline int screen_height = 1080;
inline bool menu_open = true;
inline double frame_time = 0;
inline ULONGLONG frame_ticks = 0;

inline void log(const char* format, ...)
{
    char message[512];
    strcpy_s(message, xor_text("WD_xetal: "));
    constexpr std::size_t prefix_size = sizeof("WD_xetal: ") - 1;
    va_list args;
    va_start(args, format);
    vsnprintf(message + prefix_size, sizeof(message) - prefix_size - 1, format, args);
    va_end(args);
    const auto length = std::strlen(message);
    message[length] = '\n';
    message[length + 1] = '\0';
    OutputDebugStringA(message);
}

inline bool is_userland_ptr(std::uintptr_t p)
{
    return p >= 0x10000 && p < 0x7FFFFFFFFFFF;
}

inline bool is_userland_range(std::uintptr_t p, std::size_t size)
{
    return is_userland_ptr(p) && size <= 0x7FFFFFFFFFFF - p;
}

inline bool is_readable_ptr(const void* pointer, std::size_t size = sizeof(void*))
{
    if (!size || !is_userland_range(reinterpret_cast<std::uintptr_t>(pointer), size))
        return false;
    // Match the working app: probing must not rely on this DLL's unwind metadata.
#pragma warning(suppress : 4996)
    return IsBadReadPtr(pointer, size) == FALSE;
}

inline bool is_writable_ptr(void* pointer, std::size_t size = sizeof(void*))
{
    if (!size || !is_userland_range(reinterpret_cast<std::uintptr_t>(pointer), size))
        return false;
#pragma warning(suppress : 4996)
    return IsBadWritePtr(pointer, size) == FALSE;
}

inline bool is_valid_ptr(const void* pointer, std::size_t size = sizeof(void*))
{
    return is_readable_ptr(pointer, size);
}

inline bool read_mem(std::uintptr_t address, void* output, std::size_t size)
{
    if (!is_writable_ptr(output, size))
        return false;
    const void* source = reinterpret_cast<const void*>(address);
    if (!is_readable_ptr(source, size))
    {
        std::memset(output, 0, size);
        return false;
    }
    __try
    {
        std::memcpy(output, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (is_writable_ptr(output, size))
            std::memset(output, 0, size);
        return false;
    }
}

template <typename T>
inline T read(std::uintptr_t address)
{
    static_assert(std::is_trivially_copyable_v<T>);
    T value{};
    read_mem(address, &value, sizeof(value));
    return value;
}

inline bool write_mem(std::uintptr_t address, const void* data, std::size_t size)
{
    void* destination = reinterpret_cast<void*>(address);
    if (!is_readable_ptr(data, size) || !is_writable_ptr(destination, size))
        return false;
    __try
    {
        std::memcpy(destination, data, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

template <typename T>
inline bool write(std::uintptr_t address, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    return write_mem(address, &value, sizeof(value));
}
