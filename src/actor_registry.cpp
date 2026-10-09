#include "stdafx.h"
#include "actor_registry.h"

#include "engine_funcs.h"
#include "offsets.h"

#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace wdgs::actors
{
    namespace detail
    {

        struct Definition
        {
            const wchar_t* base_name;
            const char* label;
            Kind kind;
            FNameValue value{};
        };

        Definition g_definitions[] = {
            {xor_text(L"B_WDCore_MoverPlayerCharacter_C"), xor_text("Player"), Kind::player, {}},
            {xor_text(L"GC_Player_DroppedItem_Stop_C"), xor_text("Item"), Kind::dropped_item, {}},
            {xor_text(L"WDRotaryVehicle"), xor_text("Chopper"), Kind::heli, {}},
            {xor_text(L"WDVehicleWeaponExtension"), xor_text("Chopper"), Kind::heli, {}},
            {xor_text(L"GC_Vehicle_Moving_PZH2000_C"), xor_text("SPH"), Kind::sph2, {}},
            {xor_text(L"GC_GC_Vehicle_Moving_TNK_01_C"), xor_text("Tank"), Kind::tank_la26, {}},
            {xor_text(L"GC_Vehicle_Moving_APC_C"), xor_text("APC"), Kind::apc, {}},
            {xor_text(L"GC_Vehicle_Moving_Buggy_C"), xor_text("Buggy"), Kind::buggy, {}},
            {xor_text(L"GC_Vehicle_Moving_Truck_C"), xor_text("Truck"), Kind::truck, {}},
            {xor_text(L"GC_Vehicle_Moving_Boat_C"), xor_text("Boat"), Kind::boat, {}},
            {xor_text(L"GC_Vehicle_Moving_Bike_C"), xor_text("Motorcycle"), Kind::motorcycle, {}},
            {xor_text(L"B_PhysicalContainer_C"), xor_text("PContainer"), Kind::backpack, {}},
            {xor_text(L"WDPlayerInventoryContainer"), xor_text("Container"), Kind::backpack, {}}};
        std::size_t g_definition_count = sizeof(g_definitions) / sizeof(g_definitions[0]);

        /*   Definition g_definitions[] = {
              {L"B_WDCore_MoverPlayerCharacter_C", "Player", Kind::player, {}},
              {L"GC_Player_DroppedItem_Stop_C", "Dropped Item", Kind::dropped_item, {}},
              {L"WDRotaryVehicle", "Heli", Kind::heli, {}},
              {L"WDVehicleWeaponExtension", "Heli", Kind::heli, {}},
              {L"GC_Vehicle_Moving_PZH2000_C", "SPH2", Kind::sph2, {}},
              {L"GC_GC_Vehicle_Moving_TNK_01_C", "Tank LA26", Kind::tank_la26, {}},
              {L"GC_Vehicle_Moving_APC_C", "APC", Kind::apc, {}},
              {L"GC_Vehicle_Moving_Buggy_C", "Buggy", Kind::buggy, {}},
              {L"GC_Vehicle_Moving_Truck_C", "Truck", Kind::truck, {}},
              {L"GC_Vehicle_Moving_Boat_C", "Boat", Kind::boat, {}},
              {L"GC_Vehicle_Moving_Bike_C", "Motorcycle", Kind::motorcycle, {}},
              {L"B_PhysicalContainer_C", "Backpack", Kind::backpack, {}},
              {L"WDPlayerInventoryContainer", "Backpack", Kind::backpack, {}}};
          std::size_t g_definition_count = sizeof(g_definitions) / sizeof(g_definitions[0]);*/

    } // namespace detail

    static std::unordered_set<std::uintptr_t> g_vehicle_classes;
    static std::unordered_set<std::uintptr_t> g_non_vehicle_classes;
    static std::unordered_set<std::uintptr_t> g_stationary_classes;
    static std::unordered_set<std::uintptr_t> g_non_stationary_classes;
    // A class can expose more than one assembly variant.  Cache by class and
    // resolved tag, and never cache a transient empty result from a loading frame.
    static std::unordered_map<std::uint64_t, std::string> g_vehicle_labels;
    static std::unordered_map<std::uint32_t, std::string> g_stationary_labels;

    void reset()
    {
        for (std::size_t i = 0; i < detail::g_definition_count; ++i)
            detail::g_definitions[i].value = {};
        g_vehicle_classes.clear();
        g_non_vehicle_classes.clear();
        g_stationary_classes.clear();
        g_non_stationary_classes.clear();
        g_vehicle_labels.clear();
        g_stationary_labels.clear();
    }

    bool resolve_names()
    {
        bool all_resolved = true;
        for (std::size_t i = 0; i < detail::g_definition_count; ++i)
        {
            detail::Definition& definition = detail::g_definitions[i];
            if (definition.value.ComparisonIndex == 0)
                definition.value = engine_funcs::conv_string_to_name(FString(definition.base_name));
            if (definition.value.ComparisonIndex == 0)
                all_resolved = false;
        }
        return all_resolved;
    }

    Match classify(FNameValue actor_name)
    {
        if (actor_name.ComparisonIndex == 0)
            return {};

        for (std::size_t i = 0; i < detail::g_definition_count; ++i)
        {
            const detail::Definition& definition = detail::g_definitions[i];
            if (definition.value.ComparisonIndex != 0 && actor_name.ComparisonIndex == definition.value.ComparisonIndex)
                return {definition.kind, definition.label};
        }
        return {};
    }

    std::uint32_t comparison_index(Kind kind)
    {
        for (std::size_t i = 0; i < detail::g_definition_count; ++i)
        {
            const detail::Definition& definition = detail::g_definitions[i];
            if (definition.kind == kind)
                return definition.value.ComparisonIndex;
        }
        return 0;
    }

    bool is_vehicle(Kind kind)
    {
        return kind == Kind::heli || kind == Kind::sph2 || kind == Kind::tank_la26 || kind == Kind::apc || kind == Kind::buggy || kind == Kind::truck || kind == Kind::boat || kind == Kind::motorcycle || kind == Kind::stationary;
    }

    static bool fname_to_narrow(FNameValue name, char* out, std::size_t out_size);

    static bool is_stationary_vehicle_class(std::uintptr_t actor_class)
    {
        if (!is_valid_ptr(reinterpret_cast<void*>(actor_class)))
            return false;
        if (g_stationary_classes.count(actor_class))
            return true;
        if (g_non_stationary_classes.count(actor_class))
            return false;

        const FNameValue class_name = read<FNameValue>(actor_class + offsets::UObject::NamePrivate);
        char class_name_narrow[256]{};
        // Do not permanently classify a class as non-stationary while the name
        // pool is still warming up during construction/loading.
        if (!fname_to_narrow(class_name, class_name_narrow, sizeof(class_name_narrow)))
            return false;
        const std::string name(class_name_narrow);
        const bool result = name.find(xor_text("StationaryVehicle")) != std::string::npos;
        if (result)
            g_stationary_classes.insert(actor_class);
        else
            g_non_stationary_classes.insert(actor_class);
        return result;
    }

    bool is_modular_vehicle(std::uintptr_t actor)
    {
        void* base_class = engine_funcs::get_vehicle_base_class();
        if (!base_class)
            return false;

        std::uintptr_t actor_class = read<std::uintptr_t>(actor + offsets::UObject::ClassPrivate);
        if (!is_valid_ptr(reinterpret_cast<void*>(actor_class)))
            return false;

        if (g_vehicle_classes.count(actor_class))
            return true;
        if (g_non_vehicle_classes.count(actor_class))
            return false;

        bool result = engine_funcs::object_is_a(reinterpret_cast<void*>(actor), base_class);

        if (result)
            g_vehicle_classes.insert(actor_class);
        else
            g_non_vehicle_classes.insert(actor_class);
        return result;
    }

    static bool fname_to_narrow(FNameValue name, char* out, std::size_t out_size)
    {
        if (out_size == 0)
            return false;
        out[0] = '\0';
        if (name.ComparisonIndex == 0)
            return false;

        FString s = engine_funcs::conv_name_to_string(name);
        if (!s.IsValid())
            return false;
        std::wstring wide = s.ToWString(255);
        engine_funcs::release_string(s);
        if (wide.empty())
            return false;

        std::size_t len = wide.size() < out_size - 1 ? wide.size() : out_size - 1;
        for (std::size_t i = 0; i < len; i++)
            out[i] = static_cast<char>(wide[i]);
        out[len] = '\0';
        return true;
    }

    static void build_vehicle_label(FNameValue tag_name, char* buf, std::size_t buf_size)
    {
        buf[0] = '\0';

        char tag_narrow[256]{};
        if (!fname_to_narrow(tag_name, tag_narrow, sizeof(tag_narrow)))
            return;

        // Tag: "Vehicle.Variant.Air.Rotary.Littlebird.Default" → segments split by '.'
        // 0=Vehicle 1=Variant 2=Category 3=Type 4=NAME [5=VARIANT]
        const char* segments[7]{};
        int seg_count = 0;
        segments[0] = tag_narrow;
        for (char* p = tag_narrow; *p; p++)
        {
            if (*p == '.' && seg_count < 6)
            {
                *p = '\0';
                segments[++seg_count] = p + 1;
            }
        }

        const char* family = (seg_count >= 4 && segments[4][0]) ? segments[4] : tag_narrow;
        const char* variant = (seg_count >= 5 && segments[5][0]) ? segments[5] : nullptr;
        bool is_default = variant && (_stricmp(variant, xor_text("Default")) == 0 || _stricmp(variant, xor_text("Variant")) == 0);

        if (variant && !is_default)
        {
            // Abbreviate CamelCase: "MountedMachineGuns" → "MM Guns"
            // Take first letter of each word except last, then space + last word.
            char abbrev[64]{};
            std::size_t word_starts[16]{};
            int word_count = 0;
            for (const char* p = variant; *p && word_count < 16; p++)
            {
                if (p == variant || (*p >= 'A' && *p <= 'Z'))
                    word_starts[word_count++] = p - variant;
            }

            if (word_count > 1)
            {
                std::size_t out = 0;
                for (int i = 0; i < word_count - 1 && out < sizeof(abbrev) - 1; i++)
                    abbrev[out++] = variant[word_starts[i]];
                if (out < sizeof(abbrev) - 1)
                    abbrev[out++] = ' ';
                const char* last_word = variant + word_starts[word_count - 1];
                while (*last_word && out < sizeof(abbrev) - 1)
                    abbrev[out++] = *last_word++;
                abbrev[out] = '\0';
                snprintf(buf, buf_size, xor_text("%s %s"), family, abbrev);
            }
            else
            {
                snprintf(buf, buf_size, xor_text("%s %s"), family, variant);
            }
        }
        else
            strncpy_s(buf, buf_size, family, _TRUNCATE);
    }

    static bool is_generic_vehicle_label(const char* label)
    {
        if (!label || !*label)
            return true;
        // A loading/default VariantTag can resolve to only "Vehicle" (or the
        // unparsed "Vehicle.*" path).  Treat that as no label so the stationary
        // mesh fallback gets a chance to resolve the actual model name.
        return _stricmp(label, xor_text("Vehicle")) == 0 ||
               _strnicmp(label, xor_text("Vehicle."), 8) == 0;
    }

    const char* try_get_vehicle_label(std::uintptr_t actor)
    {
        std::uintptr_t actor_class = read<std::uintptr_t>(actor + offsets::UObject::ClassPrivate);
        if (!is_valid_ptr(reinterpret_cast<void*>(actor_class)))
            return nullptr;

        // Verify the class before reading its modular variant.
        if (!is_modular_vehicle(actor))
        {
            return nullptr;
        }

        std::uintptr_t variant = read<std::uintptr_t>(actor + offsets::AModularVehicle::AssemblyVariantRef);
        if (!is_valid_ptr(reinterpret_cast<void*>(variant)))
            return nullptr;

        FNameValue tag_name = read<FNameValue>(variant + offsets::UModularVehicleVariant::VariantTag);
        if (tag_name.ComparisonIndex == 0)
            return nullptr;

        const std::uint64_t cache_key = static_cast<std::uint64_t>(actor_class) ^
                                        (static_cast<std::uint64_t>(tag_name.ComparisonIndex) << 32);
        auto cached = g_vehicle_labels.find(cache_key);
        if (cached != g_vehicle_labels.end())
            return cached->second.c_str();

        char buf[128]{};
        build_vehicle_label(tag_name, buf, sizeof(buf));
        if (is_generic_vehicle_label(buf))
            return nullptr;

        auto inserted = g_vehicle_labels.emplace(cache_key, std::string(buf));
        return inserted.first->second.c_str();
    }

    const char* try_get_stationary_vehicle_label(std::uintptr_t actor)
    {
        const std::uintptr_t actor_class = read<std::uintptr_t>(actor + offsets::UObject::ClassPrivate);
        if (!is_stationary_vehicle_class(actor_class))
            return nullptr;

        const std::uintptr_t root = read<std::uintptr_t>(actor + offsets::AActor::RootComponent);
        if (!is_valid_ptr(reinterpret_cast<void*>(root)))
            return nullptr;

        // Stationary vehicles expose the visible mesh directly from their root
        // component.  The mesh asset name is stable even when the actor class is
        // the generic WDStationaryVehicle class.
        const std::uintptr_t mesh_asset = read<std::uintptr_t>(root + offsets::WDSkeletalMeshComponentBudgeted::SkinnedAsset);
        if (!is_valid_ptr(reinterpret_cast<void*>(mesh_asset)))
            return nullptr;

        const FNameValue mesh_name = read<FNameValue>(mesh_asset + offsets::UObject::NamePrivate);
        if (mesh_name.ComparisonIndex == 0)
            return nullptr;

        auto cached = g_stationary_labels.find(mesh_name.ComparisonIndex);
        if (cached != g_stationary_labels.end())
            return cached->second.c_str();

        char mesh_name_narrow[256]{};
        fname_to_narrow(mesh_name, mesh_name_narrow, sizeof(mesh_name_narrow));
        const std::string name(mesh_name_narrow);
        const char* label = nullptr;
        if (name.rfind(xor_text("SK_STN_05"), 0) == 0)
            label = xor_text("Stingray");
        else if (name.rfind(xor_text("SK_Deployable_Mortar"), 0) == 0 || name.rfind(xor_text("SK_STN_03"), 0) == 0)
            label = xor_text("L81 Mortar");
        else if (name.rfind(xor_text("SK_Mistral_Anti_Aircraft"), 0) == 0)
            label = xor_text("Talon 9K-SAM");
        else if (name.rfind(xor_text("SK_Phalanx_CIWS"), 0) == 0)
            label = xor_text("Vanguard CIWS");
        else if (name.rfind(xor_text("SK_STN_04"), 0) == 0)
            label = xor_text("Loudspeaker");

        if (!label)
            return nullptr;

        auto inserted = g_stationary_labels.emplace(mesh_name.ComparisonIndex, std::string(label));
        return inserted.first->second.c_str();
    }

} // namespace wdgs::actors
