#pragma once
#include "stdafx.h"

namespace offsets
{
    extern std::uintptr_t base;
    extern std::uintptr_t UWorldPtr;
    extern std::uintptr_t GNames;
    extern std::uintptr_t GObjects;

    inline constexpr std::uintptr_t GNamesRva = 0x0CB44540;
    inline constexpr std::uintptr_t GObjectsRva = 0x0CC12520;

    namespace ObjectArray
    {
        inline constexpr std::ptrdiff_t Objects = 0x00;
        inline constexpr std::ptrdiff_t MaxElements = 0x10;
        inline constexpr std::ptrdiff_t NumElements = 0x14;
        inline constexpr std::ptrdiff_t MaxChunks = 0x18;
        inline constexpr std::ptrdiff_t NumChunks = 0x1C;
    } // namespace ObjectArray

    inline constexpr std::int32_t ChunkSize = 0x10000;
    inline constexpr std::int32_t FUObjectItemSize = 0x18;
    inline constexpr std::int32_t FUObjectItemObjectOffset = 0x08;

    namespace UObject
    {
        inline constexpr std::ptrdiff_t VTable = 0x00;
        inline constexpr std::ptrdiff_t ObjectFlags = 0x08;
        inline constexpr std::ptrdiff_t InternalIndex = 0x0C;
        inline constexpr std::ptrdiff_t ClassPrivate = 0x10;
        inline constexpr std::ptrdiff_t NamePrivate = 0x18;
        inline constexpr std::ptrdiff_t OuterPrivate = 0x20;
    } // namespace UObject

    namespace UField
    {
        inline constexpr std::ptrdiff_t Next = 0x28;
    }

    namespace UStruct
    {
        inline constexpr std::ptrdiff_t StructBaseChain = 0x30;
        inline constexpr std::ptrdiff_t SuperStruct = 0x40;
        inline constexpr std::ptrdiff_t Children = 0x48;
        inline constexpr std::ptrdiff_t ChildProperties = 0x50;
        inline constexpr std::ptrdiff_t PropertiesSize = 0x58;
        inline constexpr std::ptrdiff_t MinAlignment = 0x5C;
    } // namespace UStruct

    namespace FField
    {
        inline constexpr std::ptrdiff_t VTable = 0x00;
        inline constexpr std::ptrdiff_t ClassPrivate = 0x08;
        inline constexpr std::ptrdiff_t Owner = 0x10;
        inline constexpr std::ptrdiff_t Next = 0x18;
        inline constexpr std::ptrdiff_t NamePrivate = 0x20;
        inline constexpr std::ptrdiff_t FlagsPrivate = 0x28;
    } // namespace FField

    namespace FFieldClass
    {
        inline constexpr std::ptrdiff_t Name = 0x00;
        inline constexpr std::ptrdiff_t Id = 0x08;
        inline constexpr std::ptrdiff_t CastFlags = 0x10;
        inline constexpr std::ptrdiff_t ClassFlags = 0x18;
        inline constexpr std::ptrdiff_t SuperClass = 0x20;
    } // namespace FFieldClass

    namespace FName
    {
        inline constexpr std::ptrdiff_t ComparisonIndex = 0x00;
        inline constexpr std::ptrdiff_t Number = 0x04;
        inline constexpr std::size_t Size = 0x08;
    } // namespace FName

    namespace FNamePool
    {
        inline constexpr std::ptrdiff_t CurrentBlock = 0x08;
        inline constexpr std::ptrdiff_t CurrentByteCursor = 0x0C;
        inline constexpr std::ptrdiff_t Blocks = 0x10;
        inline constexpr std::ptrdiff_t ChunkTable = Blocks;
        inline constexpr int BlockOffsetBits = 16;
        inline constexpr std::ptrdiff_t NameEntryStride = 0x08;
        inline constexpr std::ptrdiff_t NameEntryHeader = 0x08;
        inline constexpr std::ptrdiff_t NameEntryKind = 0x0A;
        inline constexpr std::ptrdiff_t NameEntryData = 0x0C;
    } // namespace FNamePool

    namespace FUObjectItemLayout
    {
        inline constexpr std::ptrdiff_t Object = FUObjectItemObjectOffset;
        inline constexpr std::size_t Stride = FUObjectItemSize;
    } // namespace FUObjectItemLayout

    namespace UClass
    {
        inline constexpr std::ptrdiff_t CastFlags = 0xD8;
        inline constexpr std::ptrdiff_t ClassDefaultObject = 0x110;
        inline constexpr std::ptrdiff_t ImplementedInterfaces = 0x1D8;
    } // namespace UClass

    namespace UEnum
    {
        inline constexpr std::ptrdiff_t CppType = 0x30;
        inline constexpr std::ptrdiff_t Names = 0x40;

        namespace FNameData
        {
            inline constexpr std::ptrdiff_t TaggedNames = 0x00;
            inline constexpr std::ptrdiff_t TaggedValues = 0x08;
            inline constexpr std::ptrdiff_t NumValues = 0x10;
            inline constexpr std::size_t Size = 0x18;
            inline constexpr std::uintptr_t PointerMask = ~static_cast<std::uintptr_t>(1);
        } // namespace FNameData

        inline constexpr std::ptrdiff_t CppForm = 0x58;
    } // namespace UEnum

    namespace UFunction
    {
        inline constexpr std::ptrdiff_t FunctionFlags = 0xB0;
        inline constexpr std::ptrdiff_t NumParms = 0xB4;
        inline constexpr std::ptrdiff_t ParmsSize = 0xB6;
        inline constexpr std::ptrdiff_t ReturnValueOffset = 0xB8;
        inline constexpr std::ptrdiff_t ExecFunction = 0xD8;
    } // namespace UFunction

    namespace FProperty
    {
        inline constexpr std::ptrdiff_t ArrayDim = 0x30;
        inline constexpr std::ptrdiff_t ElementSize = 0x34;
        inline constexpr std::ptrdiff_t PropertyFlags = 0x38;
        inline constexpr std::ptrdiff_t Offset_Internal = 0x44;
        inline constexpr std::ptrdiff_t DerivedBase = 0x70;
    } // namespace FProperty

    namespace FBoolProperty
    {
        inline constexpr std::ptrdiff_t FieldSize = 0x70;
        inline constexpr std::ptrdiff_t ByteOffset = 0x71;
        inline constexpr std::ptrdiff_t ByteMask = 0x72;
        inline constexpr std::ptrdiff_t FieldMask = 0x73;
    } // namespace FBoolProperty

    namespace FByteProperty
    {
        inline constexpr std::ptrdiff_t Enum = 0x70;
    }

    namespace FObjectProperty
    {
        inline constexpr std::ptrdiff_t PropertyClass = 0x70;
    }

    namespace FClassProperty
    {
        inline constexpr std::ptrdiff_t MetaClass = 0x78;
    }

    namespace FStructProperty
    {
        inline constexpr std::ptrdiff_t Struct = 0x70;
    }

    namespace FArrayProperty
    {
        inline constexpr std::ptrdiff_t ArrayFlags = 0x70;
        inline constexpr std::ptrdiff_t Inner = 0x78;
    } // namespace FArrayProperty

    namespace FSetProperty
    {
        inline constexpr std::ptrdiff_t ElementProp = 0x70;
    }

    namespace FMapProperty
    {
        inline constexpr std::ptrdiff_t KeyProp = 0x70;
        inline constexpr std::ptrdiff_t ValueProp = 0x78;
    } // namespace FMapProperty

    namespace FEnumProperty
    {
        inline constexpr std::ptrdiff_t UnderlyingProp = 0x70;
        inline constexpr std::ptrdiff_t Enum = 0x78;
    } // namespace FEnumProperty

    namespace FDelegateProperty
    {
        inline constexpr std::ptrdiff_t SignatureFunction = 0x70;
    }

    namespace TArray
    {
        inline constexpr std::ptrdiff_t Data = 0x00;
        inline constexpr std::ptrdiff_t Num = 0x08;
        inline constexpr std::ptrdiff_t Max = 0x0C;
        inline constexpr std::size_t Size = 0x10;
    } // namespace TArray

    namespace PropertyFlags
    {
        inline constexpr std::uint64_t CPF_Edit = 0x0000000000000001;
        inline constexpr std::uint64_t CPF_ConstParm = 0x0000000000000002;
        inline constexpr std::uint64_t CPF_BlueprintVisible = 0x0000000000000004;
        inline constexpr std::uint64_t CPF_Net = 0x0000000000000020;
        inline constexpr std::uint64_t CPF_Parm = 0x0000000000000080;
        inline constexpr std::uint64_t CPF_OutParm = 0x0000000000000100;
        inline constexpr std::uint64_t CPF_ReturnParm = 0x0000000000000400;
        inline constexpr std::uint64_t CPF_Transient = 0x0000000000001000;
        inline constexpr std::uint64_t CPF_RepNotify = 0x0000000000004000;
    } // namespace PropertyFlags

    namespace AActor
    {
        inline constexpr std::ptrdiff_t Children = 0x1B0;
        inline constexpr std::ptrdiff_t RootComponent = 0x1C0;
    } // namespace AActor

    namespace ACharacter
    {
        inline constexpr std::ptrdiff_t Mesh = 0x390;
    }

    namespace AGameStateBase
    {
        inline constexpr std::ptrdiff_t PlayerArray = 0x2D0;
    }

    namespace APawn
    {
        inline constexpr std::ptrdiff_t PlayerState = 0x2D8;
    }

    namespace APlayerController
    {
        inline constexpr std::ptrdiff_t AcknowledgedPawn = 0x360;
        inline constexpr std::ptrdiff_t PlayerCameraManager = 0x370;
        inline constexpr std::ptrdiff_t SpectatorPawn = 0x6C8;
    } // namespace APlayerController

    namespace APlayerController_Extra
    {
        inline constexpr std::ptrdiff_t ControllerPawn = 0x2F8;
        inline constexpr std::ptrdiff_t ControllerPlayerState = 0x2C0;
        inline constexpr std::ptrdiff_t DefaultFOV = 0x2D0;
        inline constexpr std::ptrdiff_t LockedFOV = 0x2D8;
    } // namespace APlayerController_Extra

    namespace World
    {
        constexpr std::uintptr_t PersistentLevel = 0x30;
        constexpr std::uintptr_t NetDriver = 0x38;
        constexpr std::uintptr_t GameState = 0x1B0;
        constexpr std::uintptr_t Levels = 0x1c8;
        constexpr std::uintptr_t OwningGameInstance = 0x228;
    } // namespace World

    namespace UNetDriver
    {
        constexpr std::uintptr_t ClientConnections = 0x100;
    }

    namespace ULevel
    {
        constexpr std::uintptr_t Actors = 0xA0;
    }

    namespace UGameInstance
    {
        constexpr std::uintptr_t LocalPlayers = 0x38;
    }

    namespace UPlayer
    {
        constexpr std::uintptr_t PlayerController = 0x30;
    }

    namespace ULocalPlayer
    {
        constexpr std::uintptr_t ViewportClient = 0x80;
    }

    namespace UNetConnection
    {
        constexpr std::uintptr_t OwningActor = 0x98;
    }

    namespace APlayerState
    {
        constexpr std::uintptr_t PlayerId = 0x2BC;
        constexpr std::uintptr_t CompressedPing = 0x2c0;
        constexpr std::uintptr_t PawnPrivate = 0x330;
        constexpr std::uintptr_t PlayerNamePrivate = 0x350;
    } // namespace APlayerState

    namespace UWDWeaponBehaviorComponent
    {
        constexpr std::uintptr_t AimSwayState = 0x258;
        constexpr std::uintptr_t WeaponStatsData = 0x440;
        constexpr std::uintptr_t CurItem = 0x42C;
    } // namespace UWDWeaponBehaviorComponent

    namespace FWDAimSwayState
    {
        constexpr std::uintptr_t CurrentX = 0x08;
        constexpr std::uintptr_t CurrentY = 0x0C;
        constexpr std::uintptr_t CurrentZ = 0x10;
    } // namespace FWDAimSwayState

    namespace UWDWeaponStatsData
    {
        constexpr std::uintptr_t WeaponStatsInline = 0x38;
        constexpr std::uintptr_t WeaponStats = WeaponStatsInline;
        constexpr std::uintptr_t ItemTag = 0x30;
        constexpr std::uintptr_t TypicalSpeed = 0xDC;
        constexpr std::uintptr_t InitialBulletSpeed = 0xDD8;
        constexpr std::uintptr_t IronSightsAimTime = 0x18C;
        constexpr std::uintptr_t MinZeroingRange = 0x198;
        constexpr std::uintptr_t MaxZeroingRange = 0x19C;
        constexpr std::uintptr_t ZeroingStep = 0x1A0;
        constexpr std::uintptr_t RecoilPattern = 0xCD8;
        constexpr std::uintptr_t RecoilPatternYawMinMax = 0xCE8;
        constexpr std::uintptr_t RecoilPitchMax = 0xCF0;
        constexpr std::uintptr_t PostRandomYaw = 0xCF8;
        constexpr std::uintptr_t PostRandomPitch = 0xD08;
        constexpr std::uintptr_t ViewKickMultiplier = 0xBB0;
        constexpr std::uintptr_t RecoilPatternRandom = RecoilPatternYawMinMax;
        constexpr std::uintptr_t RecoilScalar = RecoilPitchMax;
        constexpr std::uintptr_t RecoilPitchRandom = PostRandomYaw;
        constexpr std::uintptr_t RecoilYawRandom = PostRandomPitch;
    } // namespace UWDWeaponStatsData

    namespace FWDWeaponStats
    {
        constexpr std::uintptr_t BallisticAngleUpperGuess = 0x90;
        constexpr std::uintptr_t BallisticAngleLowerGuess = 0x94;
        constexpr std::uintptr_t GravityScaleGuess = 0x98;
        constexpr std::uintptr_t ProjectileLifetimeGuess = 0x9C;
        constexpr std::uintptr_t UsesBallisticsGuess = 0xA0;
        constexpr std::uintptr_t TypicalSpeed = 0xA4;
        constexpr std::uintptr_t InheritVelocityGuess = 0xA8;
        constexpr std::uintptr_t RateOfFireRpmGuess = 0x138;
        constexpr std::uintptr_t DefaultFireModeGuess = 0x150;
        constexpr std::uintptr_t FireDelayGuess = 0x154;
        constexpr std::uintptr_t RecoveryRateGuess = 0x158;
        constexpr std::uintptr_t AutomaticFireGuess = 0x15C;
        constexpr std::uintptr_t MinZeroingRange = 0x160;
        constexpr std::uintptr_t MaxZeroingRange = 0x164;
        constexpr std::uintptr_t ZeroingStep = 0x168;
        constexpr std::uintptr_t ZeroingEnabledGuess = 0x16C;
        constexpr std::uintptr_t ClampZeroingGuess = 0x16D;
        constexpr std::uintptr_t AimCurveScaleGuess = 0xB78;
        constexpr std::uintptr_t FovShiftingType = 0xC08;
        constexpr std::uintptr_t FovShiftScaleGuess = 0xC0C;
        constexpr std::uintptr_t FovBlendTimeGuess = 0xC98;
        constexpr std::uintptr_t FiringAnimationAngleGuess = 0xCB8;
        constexpr std::uintptr_t MuzzleVelocity = 0xDA0;
        constexpr std::uintptr_t EffectiveRange = 0xDA4;
        constexpr std::uintptr_t SuppressionMultiplier = 0xDA8;
        constexpr std::uintptr_t MeleeDamage = 0xDAC;
        constexpr std::uintptr_t RawE38 = 0xE38;
        constexpr std::uintptr_t RawE3C = 0xE3C;
    } // namespace FWDWeaponStats

    namespace USceneComponent
    {
        constexpr std::uintptr_t AttachParent = 0xF0;
        constexpr std::uintptr_t AttachChildren = 0x108;
        constexpr std::uintptr_t ClientAttachedChildren = 0x118;
        constexpr std::uintptr_t RelativeLocation = 0x168;
        constexpr std::uintptr_t RelativeRotation = RelativeLocation + 0x18;
        constexpr std::uintptr_t RelativeVelocity = 0x1B0;
        constexpr std::uintptr_t ComponentToWorld = 0x210;
    } // namespace USceneComponent

    namespace APlayerCameraManager
    {
        constexpr std::uintptr_t ViewTarget = 0x350;
        constexpr std::uintptr_t FminimalViewInfo = ViewTarget + 0x10;
        constexpr std::uintptr_t PendingViewTarget = 0xc40;
        constexpr std::uintptr_t CameraCachePrivate = 0x1560;
        constexpr std::uintptr_t CameraCachePublic = 0x1E40;
    } // namespace APlayerCameraManager

    namespace FTViewTarget
    {
        constexpr std::uintptr_t Target = 0x0;
        constexpr std::uintptr_t POV = 0x10;
    } // namespace FTViewTarget

    namespace FMinimalViewInfo
    {
        constexpr std::uintptr_t Location = 0x0;
        constexpr std::uintptr_t Rotation = 0x18;
        constexpr std::uintptr_t FOV = 0x30;
        constexpr std::uintptr_t AspectRatio = 0x5C;
    } // namespace FMinimalViewInfo

    namespace FCameraCacheEntry
    {
        constexpr std::uintptr_t POV = 0x10;
    }

    namespace UPrimitiveComponent
    {
        constexpr std::uintptr_t BoundsScale = 0x344;
        constexpr std::uintptr_t LastSubmitTime = BoundsScale + 0x4;
        constexpr std::uintptr_t LastSubmitTimeOnScreen = BoundsScale + 0x8;
        constexpr std::uintptr_t LastRenderTime = 0x35C;
    } // namespace UPrimitiveComponent

    namespace USkinnedMeshComponent
    {
        constexpr std::uintptr_t bCachedWorldSpaceBoundsUpToDate = 0x81a;
        constexpr std::uintptr_t CachedWorldOrLocalSpaceBounds = 0x870;
    } // namespace USkinnedMeshComponent

    namespace USkeletalMeshComponent
    {
        constexpr std::uintptr_t CachedComponentSpaceTransforms = 0xA18;
        constexpr std::uintptr_t CachedBoneSpaceTransforms = CachedComponentSpaceTransforms - 0x10;
    } // namespace USkeletalMeshComponent

    namespace WDSkeletalMeshComponentBudgeted
    {
        inline constexpr std::ptrdiff_t SkinnedAsset = 0x5A8;
        inline constexpr std::ptrdiff_t SkinnedAssetSecondary = 0x5B0;
        inline constexpr std::ptrdiff_t BoneArray = 0x620;
        inline constexpr std::ptrdiff_t BoneArrayCache = 0x630;
        inline constexpr std::size_t BoneTransformStride = 0x60;
    } // namespace WDSkeletalMeshComponentBudgeted

    namespace USkeletalMesh
    {
        inline constexpr std::ptrdiff_t Skeleton = 0x118;
        inline constexpr std::ptrdiff_t Sockets = 0x4B8;
    } // namespace USkeletalMesh

    namespace AWDPlayerStateSession
    {
        inline constexpr std::ptrdiff_t SquadId = 0x140;
        inline constexpr std::ptrdiff_t FactionComponent = 0x548;
        inline constexpr std::ptrdiff_t SquadComponent = 0x550;
        inline constexpr std::ptrdiff_t IsDeveloper = 0x619;
        inline constexpr std::ptrdiff_t IsAdmin = 0x61A;
    } // namespace AWDPlayerStateSession

    namespace UWDFactionComponent
    {
        inline constexpr std::ptrdiff_t FactionTag = 0x110;
        inline constexpr std::ptrdiff_t FactionObject = 0x128;
    } // namespace UWDFactionComponent

    namespace AWDMoverCharacter
    {
        inline constexpr std::ptrdiff_t VitalityComponent = 0x738;
        inline constexpr std::ptrdiff_t PlayerInventoryComponent = 0x748;
        inline constexpr std::ptrdiff_t VehicleOperator = 0x758;
        inline constexpr std::ptrdiff_t WeaponBehaviorComponent = 0x770;
        inline constexpr std::ptrdiff_t LastDamageEvent = 0x788;
        inline constexpr std::ptrdiff_t AimingAlpha = 0x81D;
        inline constexpr std::ptrdiff_t VitalityState = 0x7F4;
    } // namespace AWDMoverCharacter

    namespace FWDCharacterVitalityState
    {
        inline constexpr std::ptrdiff_t VitalityTag = 0x08;
        inline constexpr std::ptrdiff_t Health = 0x14;
        inline constexpr std::ptrdiff_t HealthMax = 0x18;
        inline constexpr std::ptrdiff_t Bleedout = 0x1C;
        inline constexpr std::ptrdiff_t Stance = 0x1D;
    } // namespace FWDCharacterVitalityState

    namespace UWDPlayerInventoryComponent
    {
        inline constexpr std::ptrdiff_t HeldItem = 0x8C8;
    }

    namespace AWDPlayerInventoryContainer
    {
        inline constexpr std::ptrdiff_t FloorImpactPoint = 0x328;
        inline constexpr std::ptrdiff_t BackpackItemTag = 0x340;
    } // namespace AWDPlayerInventoryContainer

    namespace UWDVehicleOperatorComponent
    {
        inline constexpr std::ptrdiff_t CurrentSeat = 0x188;
        inline constexpr std::ptrdiff_t ReplicatedSeat = 0x198;
    } // namespace UWDVehicleOperatorComponent

    namespace UWDWeaponManagerComponent
    {
        inline constexpr std::ptrdiff_t Weapons = 0xF0;
    }

    namespace UWDVehicleWeaponManagerComponent
    {
        inline constexpr std::ptrdiff_t WeaponArray = 0xF0;
    }

    namespace AWDVehicleWeapon
    {
        inline constexpr std::ptrdiff_t OwningVehicle = 0x320;
        inline constexpr std::ptrdiff_t WeaponExtensions = 0x338;
        // Generated SDK: the weapon owns the visible skeletal mesh and its
        // muzzle socket component separately from the actor root.
        inline constexpr std::ptrdiff_t SkeletalMesh = 0x370;
        inline constexpr std::ptrdiff_t SceneComponent = 0x378;
        inline constexpr std::ptrdiff_t RotationComponent = 0x3D8;
    } // namespace AWDVehicleWeapon

    namespace UWDVehicleWeaponExtension
    {
        inline constexpr std::ptrdiff_t DataAsset = 0x358;
    }

    namespace UWDVehicleWeaponData
    {
        inline constexpr std::ptrdiff_t WeaponType = 0x2F0;
        inline constexpr std::ptrdiff_t ArtilleryRangeData = 0x3C8;
    } // namespace UWDVehicleWeaponData

    namespace AModularVehicle
    {
        inline constexpr std::ptrdiff_t AssemblyData = 0x03A0;
        inline constexpr std::ptrdiff_t AssemblyVariantRef = 0x03A8;
    } // namespace AModularVehicle

    namespace UModularVehicleVariant
    {
        inline constexpr std::ptrdiff_t VariantTag = 0x0034;
    }

    namespace ABHBaseVehiclePawn
    {
        inline constexpr std::ptrdiff_t DamageModelComponent = 0x0788;
    }

    namespace UWDWeaponRotationComponent
    {
        inline constexpr std::ptrdiff_t TargetRotation = 0x260;
        inline constexpr std::ptrdiff_t LiveWorldRotation = 0x350;
    } // namespace UWDWeaponRotationComponent

    namespace UWDVehicleWeaponRotationComponent
    {
        inline constexpr std::ptrdiff_t BarrelRotation = 0x260;
    }

    namespace ABHRotaryVehiclePawn
    {
        inline constexpr std::ptrdiff_t MovementComponent = 0xA80;
    }

    namespace UBHRotaryMovementComponent
    {
        inline constexpr std::ptrdiff_t FlightModelAssistance = 0x4D8;
        inline constexpr std::ptrdiff_t FlightControlState = 0x558;
        inline constexpr std::ptrdiff_t CurrentAltitudeCm = 0x57C;
    } // namespace UBHRotaryMovementComponent

    namespace VehicleWeaponManagerOffset
    {
        inline constexpr std::ptrdiff_t Rotary = 0xB90;
        inline constexpr std::ptrdiff_t Airplane = 0xBB0;
        inline constexpr std::ptrdiff_t Tracked = 0xBF0;
        inline constexpr std::ptrdiff_t Wheeled = 0xBE0;
    } // namespace VehicleWeaponManagerOffset

    namespace UWDVitalityComponent
    {
        inline constexpr std::ptrdiff_t BaseHealth = 0x130;
        inline constexpr std::ptrdiff_t MaxHealth = 0x134;
    } // namespace UWDVitalityComponent

    namespace UWDCharacterVitalityComponent
    {
        inline constexpr std::ptrdiff_t CurrentBaseHealth = 0x160;
        inline constexpr std::ptrdiff_t CurrentDecayHealth = 0x164;
        inline constexpr std::ptrdiff_t CurrentOverhealHealth = 0x168;
    } // namespace UWDCharacterVitalityComponent

    namespace BHAttributeHandle
    {
        inline constexpr std::ptrdiff_t CurrentHealthHandle = 0x140;
        inline constexpr std::ptrdiff_t AttributeControlValidity = 0x08;
        inline constexpr std::ptrdiff_t AttributeValue = 0x20;
        inline constexpr std::ptrdiff_t AttributeValidity = 0x28;
    } // namespace BHAttributeHandle

    namespace Functions
    {
        extern std::uintptr_t ProcessEvent;
        extern std::uintptr_t StaticFindObject;
        extern std::uintptr_t GetObjectsOfClass;
        inline constexpr std::uintptr_t FNameToString = 0x1687780;
        extern std::uintptr_t FreeObjectName;
    } // namespace Functions

    namespace Globals
    {
        inline constexpr std::uintptr_t GEngine = 0xD0E6150;
        inline constexpr std::uintptr_t GDynamicRHI = 0xD017E68;
    } // namespace Globals

    std::uintptr_t pattern_scan(std::uintptr_t module_base, const char* signature);
    std::uintptr_t resolve_rip(std::uintptr_t address, int displacement_offset, int instruction_size);
    extern void setup();
} // namespace offsets
