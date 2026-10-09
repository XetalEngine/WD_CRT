#pragma once
#include "stdafx.h"
#include "structs.h"
#include "offsets.h"

class UObject;

struct FUObjectItem
{
    uintptr_t flags_and_state;  // +0x00  GC / lifecycle flags packed
    UObject* object;            // +0x08  raw pointer, unmasked
    std::int32_t serial_number; // +0x10
    std::int32_t pad;           // +0x14
};

class FUObjectsArray
{
  public:
    std::uint32_t count()
    {
        static const std::uintptr_t num_elements_va = offsets::base + offsets::GObjects + num_elements_off;
        return read<std::uint32_t>(num_elements_va);
    }

    UObject* by_index(std::uint32_t index)
    {
        const auto object_count = count();
        if (object_count == 0 || object_count > 0x4000000u || index >= object_count)
            return nullptr;

        static const std::uintptr_t chunk_table_ptr_va = offsets::base + offsets::GObjects + chunk_table_off;

        std::uintptr_t chunk_table = read<std::uintptr_t>(chunk_table_ptr_va);
        if (chunk_table == 0)
        {
            return nullptr;
        }

        std::uint32_t chunk_idx = index >> 16;
        std::uint32_t slot_idx = static_cast<std::uint16_t>(index);

        std::uintptr_t chunk = read<std::uintptr_t>(chunk_table + chunk_idx * sizeof(std::uintptr_t));
        if (chunk == 0)
        {
            return nullptr;
        }

        std::uintptr_t slot = chunk + slot_idx * sizeof(FUObjectItem);
        std::uintptr_t obj = read<std::uintptr_t>(slot + object_off);

        if (!is_valid_ptr(reinterpret_cast<const void*>(obj)))
        {
            return nullptr;
        }
        return reinterpret_cast<UObject*>(obj);
    }

    bool read_item(std::uint32_t index, FUObjectItem& out)
    {
        const auto object_count = count();
        if (object_count == 0 || object_count > 0x4000000u || index >= object_count)
            return false;

        static const std::uintptr_t chunk_table_ptr_va = offsets::base + offsets::GObjects + chunk_table_off;

        std::uintptr_t chunk_table = read<std::uintptr_t>(chunk_table_ptr_va);
        if (chunk_table == 0)
        {
            return false;
        }

        std::uintptr_t chunk = read<std::uintptr_t>(chunk_table + (index >> 16) * sizeof(std::uintptr_t));
        if (chunk == 0)
        {
            return false;
        }

        std::uintptr_t slot = chunk + static_cast<std::uint16_t>(index) * sizeof(FUObjectItem);
        return read_mem(slot, &out, sizeof(out));
    }

  private:
    static constexpr std::uintptr_t gobjects_rva = 0xCF08A60; // FChunkedFixedUObjectArray
    static constexpr std::uint32_t chunk_table_off = 0x00;    // Objects field
    static constexpr std::uint32_t num_elements_off = 0x14;   // NumElements
    static constexpr std::uint32_t object_off = 0x08;         // FUObjectItem Object
};

class FName
{
  public:
    static bool IsValid(std::uint32_t raw_index)
    {
        entry_ref_t entry{};
        if (!offsets::base || !offsets::GNames || !read_entry(raw_index, entry))
            return false;
        const auto length = char_length(entry);
        return length > 0 && length <= max_string_chars && is_readable_ptr(reinterpret_cast<const void*>(entry.address + data_off), length * (is_wide(entry) ? sizeof(wchar_t) : 1));
    }

    static std::string ToString(std::uint32_t raw_index)
    {
        return resolve(raw_index, 0);
    }

    static std::string ToString(std::uint32_t raw_index, std::uint32_t raw_number)
    {
        return resolve(raw_index, raw_number);
    }

    static std::string ToString(std::uint64_t fname_ptr)
    {
        if (fname_ptr == 0)
        {
            return {};
        }
        std::uint32_t raw_index = read<std::uint32_t>(fname_ptr + 0);
        std::uint32_t raw_number = read<std::uint32_t>(fname_ptr + 4);
        return resolve(raw_index, raw_number);
    }

    std::string ToString()
    {
        return ToString(reinterpret_cast<std::uint64_t>(this));
    }

  private:
    static constexpr std::uint32_t chunk_table_off = 0x10;
    static constexpr std::uint32_t entry_stride = 8;

    static constexpr std::uint32_t header_off = 0x08;
    static constexpr std::uint32_t kind_off = 0x0A;
    static constexpr std::uint32_t data_off = 0x0C;

    static constexpr std::uint32_t header_len_shift = 6;
    static constexpr std::uint16_t header_wide_bit = 1;
    static constexpr std::uint8_t kind_fixed_len = 2;
    static constexpr std::uint32_t kind_fixed_length = 0x16;

    static constexpr std::uint32_t max_string_chars = 1024u;
    static constexpr std::uint32_t max_chunks = 8192u;

    struct entry_ref_t
    {
        std::uint64_t address;
        std::uint16_t header;
        std::uint8_t kind;
    };

    static bool read_entry(std::uint32_t raw_index, entry_ref_t& out)
    {
        if (raw_index == 0)
        {
            return false;
        }

        const std::uint64_t chunk_table = offsets::base + offsets::GNames + chunk_table_off;
        const std::uint32_t chunk_idx = raw_index >> 16;
        const std::uint32_t slot_idx = static_cast<std::uint16_t>(raw_index);
        if (chunk_idx >= max_chunks)
            return false;

        std::uint64_t chunk = read<std::uint64_t>(chunk_table + chunk_idx * sizeof(std::uint64_t));
        if (chunk == 0)
        {
            return false;
        }

        out.address = chunk + slot_idx * entry_stride;
        out.header = read<std::uint16_t>(out.address + header_off);
        out.kind = read<std::uint8_t>(out.address + kind_off);
        return out.header != 0;
    }

    static std::uint32_t char_length(const entry_ref_t& entry)
    {
        if (entry.kind == kind_fixed_len)
        {
            return kind_fixed_length;
        }
        return static_cast<std::uint32_t>(entry.header) >> header_len_shift;
    }

    static bool is_wide(const entry_ref_t& entry)
    {
        return (entry.header & header_wide_bit) != 0;
    }

    static std::string read_narrow(std::uint64_t data_addr, std::uint32_t chars)
    {
        std::string out;
        out.resize(chars);
        if (!read_mem(data_addr, out.data(), chars))
            return {};
        return out;
    }

    static std::string read_wide(std::uint64_t data_addr, std::uint32_t chars)
    {
        std::vector<std::uint16_t> buf(chars);
        if (!read_mem(data_addr, buf.data(), chars * sizeof(std::uint16_t)))
            return {};

        std::string out;
        out.resize(chars);
        for (std::uint32_t i = 0; i < chars; ++i)
        {
            out[i] = static_cast<char>(buf[i] & 0xFF);
        }
        return out;
    }

    static void append_number(std::string& s, std::uint32_t raw_number)
    {
        if (raw_number == 0)
        {
            return;
        }
        s += xor_text("_");
        s += std::to_string(raw_number - 1u);
    }

    static std::string resolve(std::uint32_t raw_index, std::uint32_t raw_number)
    {
        entry_ref_t entry{};
        if (!read_entry(raw_index, entry))
        {
            return {};
        }

        std::uint32_t chars = char_length(entry);
        if (chars == 0 || chars > max_string_chars)
        {
            return {};
        }

        std::string result = is_wide(entry)
                                 ? read_wide(entry.address + data_off, chars)
                                 : read_narrow(entry.address + data_off, chars);

        append_number(result, raw_number);
        return result;
    }
};

class UObject
{
  public:
    std::uintptr_t addr() const
    {
        return reinterpret_cast<std::uintptr_t>(this);
    }

    std::int32_t GetFNameIndex()
    {
        return read<std::int32_t>(addr() + offsets::UObject::NamePrivate);
    }

    std::string GetName()
    {
        return FName::ToString(static_cast<std::uint32_t>(GetFNameIndex()));
    }
};

class AActor : public UObject
{
  public:
    std::uintptr_t RootComponent()
    {
        return read<std::uintptr_t>(addr() + offsets::AActor::RootComponent);
    }
};

class APawn : public AActor
{
  public:
    std::uintptr_t PlayerState()
    {
        return read<std::uintptr_t>(addr() + offsets::APawn::PlayerState);
    }
};

class APlayerController : public UObject
{
  public:
    std::uintptr_t AcknowledgedPawn()
    {
        return read<std::uintptr_t>(addr() + offsets::APlayerController::AcknowledgedPawn);
    }

    std::uintptr_t PlayerCameraManager()
    {
        return read<std::uintptr_t>(addr() + offsets::APlayerController::PlayerCameraManager);
    }
};

class APlayerState : public UObject
{
  public:
    std::uint8_t CompressedPing()
    {
        return read<std::uint8_t>(addr() + offsets::APlayerState::CompressedPing);
    }

    std::uintptr_t PawnPrivate()
    {
        return read<std::uintptr_t>(addr() + offsets::APlayerState::PawnPrivate);
    }

    std::wstring PlayerName()
    {
        FString fs = read<FString>(addr() + offsets::APlayerState::PlayerNamePrivate);
        return fs.ToWString(64);
    }
};

class UWorld : public UObject
{
  public:
    std::uintptr_t PersistentLevel()
    {
        return read<std::uintptr_t>(addr() + offsets::World::PersistentLevel);
    }

    std::uintptr_t NetDriver()
    {
        return read<std::uintptr_t>(addr() + offsets::World::NetDriver);
    }

    std::uintptr_t GameState()
    {
        return read<std::uintptr_t>(addr() + offsets::World::GameState);
    }

    std::uintptr_t OwningGameInstance()
    {
        return read<std::uintptr_t>(addr() + offsets::World::OwningGameInstance);
    }
};

class ULevel : public UObject
{
  public:
    TArray<std::uintptr_t> Actors()
    {
        TArray<std::uintptr_t> arr = read<TArray<std::uintptr_t>>(addr() + offsets::ULevel::Actors);
        return arr;
    }
};

class UGameInstance : public UObject
{
  public:
    TArray<std::uintptr_t> LocalPlayers()
    {
        TArray<std::uintptr_t> arr = read<TArray<std::uintptr_t>>(addr() + offsets::UGameInstance::LocalPlayers);
        return arr;
    }
};

class UPlayer : public UObject
{
  public:
    std::uintptr_t PlayerController()
    {
        return read<std::uintptr_t>(addr() + offsets::UPlayer::PlayerController);
    }
};

class ULocalPlayer : public UPlayer
{
  public:
    std::uintptr_t ViewportClient()
    {
        return read<std::uintptr_t>(addr() + offsets::ULocalPlayer::ViewportClient);
    }
};

class UNetConnection : public UPlayer
{
  public:
    std::uintptr_t OwningActor()
    {
        return read<std::uintptr_t>(addr() + offsets::UNetConnection::OwningActor);
    }
};

class UNetDriver : public UObject
{
  public:
    TArray<std::uintptr_t> ClientConnections()
    {
        TArray<std::uintptr_t> arr = read<TArray<std::uintptr_t>>(addr() + offsets::UNetDriver::ClientConnections);
        return arr;
    }
};

class USceneComponent : public UObject
{
  public:
    FVector RelativeLocation()
    {
        return read<FVector>(addr() + offsets::USceneComponent::RelativeLocation);
    }

    FRotator RelativeRotation()
    {
        return read<FRotator>(addr() + offsets::USceneComponent::RelativeRotation);
    }

    FTransform ComponentToWorld()
    {
        return read<FTransform>(addr() + offsets::USceneComponent::ComponentToWorld);
    }

    FVector GetWorldLocation()
    {
        return ComponentToWorld().Translation;
    }
};

class UPrimitiveComponent : public USceneComponent
{
  public:
    float BoundsScale()
    {
        return read<float>(addr() + offsets::UPrimitiveComponent::BoundsScale);
    }

    float LastSubmitTime()
    {
        return read<float>(addr() + offsets::UPrimitiveComponent::LastSubmitTime);
    }

    float LastSubmitTimeOnScreen()
    {
        return read<float>(addr() + offsets::UPrimitiveComponent::LastSubmitTimeOnScreen);
    }
};

class USkinnedMeshComponent : public UPrimitiveComponent
{
  public:
    FBoxSphereBounds CachedWorldOrLocalSpaceBounds()
    {
        return read<FBoxSphereBounds>(addr() + offsets::USkinnedMeshComponent::CachedWorldOrLocalSpaceBounds);
    }
};

class USkeletalMeshComponent : public USkinnedMeshComponent
{
  public:
    TArray<FTransform> CachedBoneSpaceTransforms()
    {
        TArray<FTransform> arr = read<TArray<FTransform>>(addr() + offsets::USkeletalMeshComponent::CachedBoneSpaceTransforms);
        return arr;
    }

    TArray<FTransform> CachedComponentSpaceTransforms()
    {
        TArray<FTransform> arr = read<TArray<FTransform>>(addr() + offsets::USkeletalMeshComponent::CachedComponentSpaceTransforms);
        return arr;
    }

    FVector GetBoneLocation(int bone_index)
    {
        auto transforms = CachedComponentSpaceTransforms();
        if (transforms.Data == 0 || bone_index < 0 || bone_index >= transforms.Count)
            return {};
        FTransform bone = read<FTransform>(reinterpret_cast<std::uintptr_t>(transforms.Data) + bone_index * sizeof(FTransform));
        FTransform comp = read<FTransform>(addr() + offsets::USceneComponent::ComponentToWorld);
        return FVector(comp.Translation.X + bone.Translation.X, comp.Translation.Y + bone.Translation.Y, comp.Translation.Z + bone.Translation.Z);
    }
};

class APlayerCameraManager : public UObject
{
  public:
    FVector GetCameraLocation()
    {
        return read<FVector>(addr() + offsets::APlayerCameraManager::FminimalViewInfo + offsets::FMinimalViewInfo::Location);
    }

    FRotator GetCameraRotation()
    {
        return read<FRotator>(addr() + offsets::APlayerCameraManager::FminimalViewInfo + offsets::FMinimalViewInfo::Rotation);
    }

    float GetCameraFOV()
    {
        return read<float>(addr() + offsets::APlayerCameraManager::FminimalViewInfo + offsets::FMinimalViewInfo::FOV);
    }
};
