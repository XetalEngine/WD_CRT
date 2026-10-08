#pragma once
#include "stdafx.h"
#include <cstdint>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

struct FVector
{
    double X, Y, Z;

    FVector()
        : X(0), Y(0), Z(0)
    {
    }
    FVector(double x, double y, double z)
        : X(x), Y(y), Z(z)
    {
    }

    FVector operator-(const FVector& o) const
    {
        return {X - o.X, Y - o.Y, Z - o.Z};
    }
    FVector operator+(const FVector& o) const
    {
        return {X + o.X, Y + o.Y, Z + o.Z};
    }
    FVector operator*(double s) const
    {
        return {X * s, Y * s, Z * s};
    }

    double Length() const
    {
        return std::sqrt(X * X + Y * Y + Z * Z);
    }
    double Distance(const FVector& o) const
    {
        return (*this - o).Length();
    }
};

struct FVector2D
{
    double X, Y;

    FVector2D()
        : X(0), Y(0)
    {
    }
    FVector2D(double x, double y)
        : X(x), Y(y)
    {
    }

    FVector2D operator-(const FVector2D& o) const
    {
        return {X - o.X, Y - o.Y};
    }
    FVector2D operator+(const FVector2D& o) const
    {
        return {X + o.X, Y + o.Y};
    }
    FVector2D operator*(double s) const
    {
        return {X * s, Y * s};
    }

    double Length() const
    {
        return std::sqrt(X * X + Y * Y);
    }
    double Distance(const FVector2D& o) const
    {
        return (*this - o).Length();
    }
};

struct FRotator
{
    double Pitch, Yaw, Roll;
};

struct FTransform
{
    struct
    {
        double X, Y, Z, W;
    } Rotation;
    FVector Translation;
    double pad0;
    FVector Scale3D;
    double pad1;
};

struct FNameValue
{
    std::uint32_t ComparisonIndex;
    std::uint32_t Number;
};

struct FBoxSphereBounds
{
    FVector Origin;
    FVector BoxExtent;
    double SphereRadius;
};

template <class T>
struct TArray
{
    friend struct FString;

  public:
    inline TArray()
    {
        Data = nullptr;
        Count = Max = 0;
    };

    inline int Num() const
    {
        return Count;
    };

    inline T operator[](int i) const
    {
        T value{};
        if (i >= 0 && i < Count && Count <= Max && Data)
            read_mem(reinterpret_cast<std::uintptr_t>(Data) + static_cast<std::size_t>(i) * sizeof(T), &value, sizeof(T));
        return value;
    };

    inline bool IsValidIndex(int i) const
    {
        return i >= 0 && i < Num();
    }

    inline bool IsSane(int max_allowed) const
    {
        if (Count < 0 || Max < 0 || Count > Max || Max > max_allowed)
            return false;
        if (Count == 0)
            return true;
        if (!Data || static_cast<std::size_t>(Count) > (std::numeric_limits<std::size_t>::max)() / sizeof(T))
            return false;
        return is_valid_ptr(Data, static_cast<std::size_t>(Count) * sizeof(T));
    }

    inline bool TryGet(int i, T& out, int max_allowed) const
    {
        if (Count < 0 || Max < 0 || Count > Max || Max > max_allowed || !Data || !IsValidIndex(i))
            return false;
        return read_mem(reinterpret_cast<std::uintptr_t>(Data) + static_cast<std::size_t>(i) * sizeof(T), &out, sizeof(T));
    }

    inline int Slack() const
    {
        return Max - Count;
    }

    __forceinline bool RemoveSingle(const int Index)
    {
        if (Index >= 0 && Index < Count && Count <= Max && Data)
        {
            if (Index != Count - 1)
            {
                T last{};
                if (!read_mem(reinterpret_cast<std::uintptr_t>(Data) + static_cast<std::size_t>(Count - 1) * sizeof(T), &last, sizeof(T)) || !write_mem(reinterpret_cast<std::uintptr_t>(Data) + static_cast<std::size_t>(Index) * sizeof(T), &last, sizeof(T)))
                    return false;
            }

            --Count;

            return true;
        }
        return false;
    }

    __forceinline void RemoveAt(int Index, int Length = 1)
    {
        for (; Length != 0; --Length)
        {
            if (!RemoveSingle(Index++))
                break;
        }
    }

  public:
    T* Data;
    int32_t Count;
    int32_t Max;
};

struct FString : private TArray<wchar_t>
{
    inline FString() {
    };

    FString(const wchar_t* other)
    {
        // This project calls StaticFindObject with a non-owning FString view.
        // Its Count/Max must describe characters only; including the trailing NUL
        // changes the lookup key and makes every lookup miss in this engine build.
        Max = Count = (other && *other) ? static_cast<std::int32_t>(wcslen(other)) : 0;

        if (Count)
        {
            Data = const_cast<wchar_t*>(other);
        }
    };

    inline bool IsValid() const
    {
        return Data != nullptr && Count > 0 && Max >= Count && Max <= 1024 &&
               is_valid_ptr(Data, static_cast<std::size_t>(Count) * sizeof(wchar_t));
    }

    inline std::wstring ToWString(std::size_t max_chars = 512) const
    {
        if (!Data || Count <= 0 || Max < Count || static_cast<std::size_t>(Count) > max_chars + 1)
            return {};

        std::vector<wchar_t> buffer(static_cast<std::size_t>(Count));
        if (!read_mem(reinterpret_cast<std::uintptr_t>(Data), buffer.data(), buffer.size() * sizeof(wchar_t)))
            return {};

        std::size_t length = buffer.size();
        if (length && buffer[length - 1] == L'\0')
            --length;
        return std::wstring(buffer.data(), length);
    }

    inline const wchar_t* c_str() const
    {
        return Data;
    }
    inline void* data()
    {
        return Data;
    }
    bool IsEmpty() const
    {
        return Count <= 0;
    }
};
