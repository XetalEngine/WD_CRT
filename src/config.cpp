#include "config.h"
#include <cmath>
#include <cstddef>
#include "overlay.h"

namespace
{
#ifdef WD_TEST
    constexpr const char* key = "Software\\WDRewriteTests";
#else
    constexpr const char* key = "Software\\WDGS\\Rewrite";
#endif
    struct Stored
    {
        DWORD version = 3;
        Config data;
    };
    static_assert(offsetof(Config, colors) == 184, "Keep the version 1 settings prefix intact");

    void limit(float& value, float low, float high, float fallback)
    {
        value = std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }

    void normalize_bool(bool& value)
    {
        value = *reinterpret_cast<const unsigned char*>(&value) != 0;
    }
} // namespace

void validate_config(Config& value)
{
    auto& e = value.esp;
    for (bool* b : {&e.enabled, &e.agent_name, &e.skeleton, &e.box, &e.lines, &e.health, &e.distance, &e.visible_check, &e.team, &e.loot, &e.vehicles, &e.minimap, &e.minimap_auto_range, &value.aimbot.enabled, &value.aimbot.draw_fov, &value.aimbot.visible_check, &value.aimbot.team_check, &value.aimbot.silent_aim, &value.aimbot.magic_bullet, &value.prediction.enabled, &value.prediction.bullet_drop, &value.prediction.velocity_lead, &value.prediction.show_line, &value.anti_sam.auto_flare, &value.anti_sam.flare_warning, &value.mortar.mortar_aim})
        normalize_bool(*b);
    e.box_style = std::clamp(e.box_style, 0, 1);
    limit(e.player_distance, 1, 2000, 750);
    limit(e.skeleton_distance, 1, 500, 200);
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
    value.aimbot.bone = std::clamp(value.aimbot.bone, 0, 4);
    value.aimbot.mode = std::clamp(value.aimbot.mode, 0, 2);
    value.aimbot.key = std::clamp(value.aimbot.key, 1, 254);
    value.aimbot.smooth = std::clamp(value.aimbot.smooth, 1, 50);
    limit(value.aimbot.fov, 1, 800, 80);
    limit(value.aimbot.magic_bullet_delay_off, 0, 10, 0);
    value.mortar.arc_mode = std::clamp(value.mortar.arc_mode, 0, 1);
    limit(value.mortar.fov, 1, 180, 30);
    limit(value.mortar.range_scale, 0.1f, 2, 0.77f);
    auto& x = value.extra;
    for (bool* b : {&x.no_recoil, &x.explosives, &x.death_bags, &x.tracers, &x.radar_directions, &x.radar_team, &x.radar_smoothing, &x.auto_join, &x.anti_afk, &x.feature_hud, &x.build_x})
        normalize_bool(*b);
    x.tracer_style = std::clamp(x.tracer_style, 0, 3);
    x.mortar_mode = std::clamp(x.mortar_mode, 0, 1);
    x.player_text = std::clamp(x.player_text, 0, 4);
    limit(x.explosive_range, 10, 1000, 200);
    limit(x.bag_range, 10, 1000, 150);
    limit(x.tracer_lifetime, 0.25f, 5, 2);
    limit(x.tracer_width, 1, 4, 1.5f);
    limit(x.radar_arrow_size, 3, 10, 5);
    for (float* color : {x.tracer_color, x.tracer_end_color, x.explosive_color, x.bag_color})
        for (int i = 0; i < 4; ++i)
            limit(color[i], 0, 1, 1);
}

bool save_config()
{
    validate_config(config);
    Stored stored{3, config};
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
    else if (stored.version == 2 && size == offsetof(Stored, data) + offsetof(Config, extra))
    {
        // New fields retain their defaults; explicit older bone choices survive.
    }
    else if (stored.version != 3 || size != sizeof(stored))
        return false;
    validate_config(stored.data);
    config = stored.data;
    return true;
}

std::string encode_config(const Config& value)
{
    Stored stored{3, value};
    validate_config(stored.data);
    const auto bytes = reinterpret_cast<const unsigned char*>(&stored);
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result = "XENGINE3:";
    result.reserve(9 + sizeof(stored) * 2 + 8);
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < sizeof(stored); ++i)
    {
        result += hex[bytes[i] >> 4];
        result += hex[bytes[i] & 15];
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    char checksum[9];
    snprintf(checksum, sizeof(checksum), "%08X", hash);
    result += checksum;
    return result;
}

bool decode_config(const char* text, std::size_t length, Config& value)
{
    if (!text || length != 9 + sizeof(Stored) * 2 + 8 || memcmp(text, "XENGINE3:", 9))
        return false;
    const auto digit = [](char c)
    { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                                   : -1; };
    Stored stored{};
    auto bytes = reinterpret_cast<unsigned char*>(&stored);
    std::uint32_t hash = 2166136261u, expected = 0;
    for (std::size_t i = 0; i < sizeof(stored); ++i)
    {
        const int high = digit(text[9 + i * 2]), low = digit(text[10 + i * 2]);
        if (high < 0 || low < 0)
            return false;
        bytes[i] = static_cast<unsigned char>((high << 4) | low);
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    for (std::size_t i = length - 8; i < length; ++i)
    {
        const int d = digit(text[i]);
        if (d < 0)
            return false;
        expected = (expected << 4) | d;
    }
    if (hash != expected || stored.version != 3)
        return false;
    validate_config(stored.data);
    value = stored.data;
    return true;
}

bool copy_config()
{
    const auto text = encode_config(config);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!memory)
        return false;
    void* data = GlobalLock(memory);
    if (!data)
    {
        GlobalFree(memory);
        return false;
    }
    memcpy(data, text.c_str(), text.size() + 1);
    GlobalUnlock(memory);
    bool ok = false;
    if (OpenClipboard(overlay::window()))
    {
        if (EmptyClipboard())
            ok = SetClipboardData(CF_TEXT, memory) != nullptr;
        CloseClipboard();
    }
    if (!ok)
        GlobalFree(memory);
    return ok;
}

bool paste_config()
{
    if (!OpenClipboard(overlay::window()))
        return false;
    HGLOBAL memory = GetClipboardData(CF_TEXT);
    const SIZE_T size = memory ? GlobalSize(memory) : 0;
    const char* text = size && size <= 16384 ? static_cast<const char*>(GlobalLock(memory)) : nullptr;
    bool ok = false;
    if (text)
    {
        const std::size_t length = strnlen(text, size);
        ok = length < size && decode_config(text, length, config);
        GlobalUnlock(memory);
    }
    CloseClipboard();
    return ok;
}
