#include "config.h"
#include <cmath>
#include <cstddef>

namespace
{
#ifdef WD_TEST
    constexpr const char* key = "Software\\WDRewriteTests";
#else
    constexpr const char* key = "Software\\WDGS\\Rewrite";
#endif
    struct Stored
    {
        DWORD version = 2;
        Config data;
    };
    static_assert(offsetof(Config, colors) == 184, "Keep the version 1 settings prefix intact");

    void limit(float& value, float low, float high, float fallback)
    {
        value = std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }

    void boolean(bool& value)
    {
        value = *reinterpret_cast<const unsigned char*>(&value) != 0;
    }
} // namespace

void validate_config(Config& value)
{
    auto& e = value.esp;
    for (bool* b : {&e.enabled, &e.agent_name, &e.skeleton, &e.box, &e.lines, &e.health, &e.distance, &e.visible_check, &e.team, &e.loot, &e.vehicles, &e.minimap, &e.minimap_auto_range, &value.aimbot.enabled, &value.aimbot.draw_fov, &value.aimbot.visible_check, &value.aimbot.team_check, &value.aimbot.silent_aim, &value.aimbot.magic_bullet, &value.prediction.enabled, &value.prediction.bullet_drop, &value.prediction.velocity_lead, &value.prediction.show_line, &value.anti_sam.auto_flare, &value.anti_sam.flare_warning, &value.mortar.mortar_aim})
        boolean(*b);
    e.box_style = std::clamp(e.box_style, 0, 1);
    limit(e.player_distance, 1, 2000, 2000);
    limit(e.skeleton_distance, 1, 500, 250);
    limit(e.loot_distance, 1, 2000, 80);
    limit(e.vehicle_distance, 1, 5000, 2000);
    limit(e.minimap_range, 25, 5000, 300);
    limit(e.minimap_size, 120, 500, 200);
    limit(e.minimap_opacity, 0.1f, 1, 0.7f);
    limit(e.minimap_x, -1, 32000, -1);
    limit(e.minimap_y, -1, 32000, -1);
    for (float* color : {e.visible_color, e.not_visible_color, e.loot_color, e.vehicle_color})
        for (int i = 0; i < 4; ++i)
            limit(color[i], 0, 1, 1);
    auto& c = value.colors;
    for (float* color : {c.team, c.dead, c.selected, c.skeleton_visible, c.skeleton_hidden, c.name_visible, c.name_hidden, c.health_full, c.health_low, c.sam, c.mortar, c.prediction, c.fov, c.warning, c.box_fill, c.label_fill, c.glow, c.radar_fill, c.radar_grid, c.radar_border, c.radar_local, c.menu_accent, c.menu_fill, c.menu_text, c.menu_value})
        for (int i = 0; i < 4; ++i)
            limit(color[i], 0, 1, 1);
    value.aimbot.bone = std::clamp(value.aimbot.bone, 0, 3);
    value.aimbot.mode = std::clamp(value.aimbot.mode, 0, 2);
    value.aimbot.key = std::clamp(value.aimbot.key, 1, 254);
    value.aimbot.smooth = std::clamp(value.aimbot.smooth, 1, 50);
    limit(value.aimbot.fov, 1, 800, 80);
    limit(value.aimbot.magic_bullet_delay_off, 0, 10, 0);
    value.mortar.arc_mode = std::clamp(value.mortar.arc_mode, 0, 1);
    limit(value.mortar.fov, 1, 180, 30);
    limit(value.mortar.range_scale, 0.1f, 2, 0.77f);
}

bool save_config()
{
    validate_config(config);
    Stored stored{2, config};
    HKEY opened;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &opened, nullptr) != ERROR_SUCCESS)
        return false;
    const LSTATUS result = RegSetValueExA(opened, "Settings", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&stored), sizeof(stored));
    RegCloseKey(opened);
    return result == ERROR_SUCCESS;
}

bool load_config()
{
    Stored stored{};
    DWORD size = sizeof(stored);
    if (RegGetValueA(HKEY_CURRENT_USER, key, "Settings", RRF_RT_REG_BINARY, nullptr, &stored, &size) != ERROR_SUCCESS)
        return false;
    if (stored.version == 1 && size == offsetof(Stored, data) + offsetof(Config, colors))
    {
        const Config defaults;
        const float old_colors[][4]{{1, 1, 1, 1}, {1, 0, 0, 1}, {0.31f, 0.82f, 1, 1}, {0, 0.7f, 0, 1}};
        float* colors[]{stored.data.esp.visible_color, stored.data.esp.not_visible_color, stored.data.esp.vehicle_color, stored.data.esp.loot_color};
        const float* new_colors[]{defaults.esp.visible_color, defaults.esp.not_visible_color, defaults.esp.vehicle_color, defaults.esp.loot_color};
        for (int i = 0; i < 4; ++i)
            if (memcmp(colors[i], old_colors[i], sizeof(old_colors[i])) == 0)
                memcpy(colors[i], new_colors[i], sizeof(old_colors[i]));
    }
    else if (stored.version != 2 || size != sizeof(stored))
        return false;
    validate_config(stored.data);
    config = stored.data;
    return true;
}
