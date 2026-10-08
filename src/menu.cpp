#include "menu.h"
#include "config.h"
#include "overlay.h"

namespace
{
    using namespace overlay;
    constexpr Color control{0.08f, 0.08f, 0.12f, 1};
    constexpr Color muted{0.60f, 0.60f, 0.60f, 1};
    constexpr float width = 660, height = 452;
    float x = 60, y = 60, mouse_x, mouse_y;
    float click_x, click_y;
    float drag_x, drag_y;
    int tab;
    bool clicked, held, dragging, stop, binding;
    bool old_insert, old_end, old_mouse, old_backspace, old_up, old_down;
    bool binding_keys[256]{};
    float* active_slider;
    const char* status = "Insert: menu    Backspace: labels    Up/Down: FOV    End: stop";

    void set_player_text(int mode)
    {
        config.extra.player_text = mode;
        config.esp.agent_name = mode == 0 || mode == 1 || mode == 3;
        config.esp.distance = mode <= 2;
    }

    void update_backspace(bool down)
    {
        if (down && !old_backspace && !binding)
            set_player_text((config.extra.player_text + 1) % 5);
        old_backspace = down;
    }

    void update_fov(bool up, bool down)
    {
        if (!binding && !(up && down))
        {
            if (up && !old_up)
                config.aimbot.fov = std::min(config.aimbot.fov + 5.f, 800.f);
            if (down && !old_down)
                config.aimbot.fov = std::max(config.aimbot.fov - 5.f, 1.f);
        }
        old_up = up;
        old_down = down;
    }

    bool inside(float a, float b, float w, float h)
    {
        return click_x >= a && click_x < a + w && click_y >= b && click_y < b + h;
    }

    void update_mouse(bool down)
    {
        held = down;
        if (held && !old_mouse)
        {
            // Keep the press until a menu frame handles it, including its position.
            clicked = true;
            click_x = mouse_x;
            click_y = mouse_y;
        }
        old_mouse = held;
    }

    bool button(float a, float b, float w, const char* label, bool selected = false)
    {
        rect(a, b, w, 24, selected ? color(config.colors.menu_accent) : control);
        text(a + w * 0.5f, b + 3, label, selected ? Color{0, 0, 0, 1} : color(config.colors.menu_value), 13, true);
        if (!clicked || !inside(a, b, w, 24))
            return false;
        clicked = false;
        return true;
    }

    void toggle(int column, int row, const char* label, bool& value)
    {
        const float a = x + 18 + column * 316, b = y + 88 + row * 30;
        text(a, b + 3, label, color(config.colors.menu_text));
        if (button(a + 246, b, 46, value ? "ON" : "OFF", value))
            value = !value;
    }

    void number(int column, int row, const char* label, float& value, float step, float low, float high, const char* fmt = "%.0f")
    {
        const float a = x + 18 + column * 316, b = y + 88 + row * 30;
        text(a, b + 3, label, color(config.colors.menu_text));
        if (button(a + 164, b, 24, "-"))
            value = std::max(low, value - step);
        if (button(a + 268, b, 24, "+"))
            value = std::min(high, value + step);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), fmt, value);
        text(a + 229, b + 3, buffer, color(config.colors.menu_value), 13, true);
    }

    void slider(int column, int row, const char* label, float& value, float low, float high, float fallback = 0, const char* fmt = "%.0f")
    {
        const float a = x + 18 + column * 316, b = y + 88 + row * 30;
        const float start = a + 128, length = 164;
        if (clicked && inside(start, b, length, 24))
        {
            value = low + std::clamp((click_x - start) / length, 0.f, 1.f) * (high - low);
            active_slider = held ? &value : nullptr;
            clicked = false;
        }
        if (active_slider == &value && held)
            value = low + std::clamp((mouse_x - start) / length, 0.f, 1.f) * (high - low);
        const float shown = std::clamp(value < 0 ? fallback : value, low, high);
        const float fill = high > low ? (shown - low) / (high - low) : 0;
        text(a, b + 3, label, color(config.colors.menu_text));
        rect(start, b + 18, length, 3, control);
        rect(start, b + 18, length * fill, 3, color(config.colors.menu_accent));
        rect(start + length * fill - 2, b + 15, 4, 9, color(config.colors.menu_value));
        char buffer[32];
        snprintf(buffer, sizeof(buffer), fmt, shown);
        text(start + length * 0.5f, b - 1, buffer, color(config.colors.menu_value), 12, true);
    }

    void integer(int column, int row, const char* label, int& value, int low, int high)
    {
        float number_value = static_cast<float>(value);
        number(column, row, label, number_value, 1, static_cast<float>(low), static_cast<float>(high));
        value = static_cast<int>(number_value);
    }

    void choice(int column, int row, const char* label, int& value, const char* const* names, int count)
    {
        const float a = x + 18 + column * 316, b = y + 88 + row * 30;
        text(a, b + 3, label, color(config.colors.menu_text));
        if (button(a + 164, b, 128, names[value]))
            value = (value + 1) % count;
    }
} // namespace

bool menu::stop_requested()
{
    return stop;
}

void menu::update()
{
    const bool insert = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    const bool end = (GetAsyncKeyState(VK_END) & 0x8000) != 0;
    if (!binding && insert && !old_insert)
        menu_open = !menu_open;
    if (!binding && end && !old_end)
        stop = true;
    old_insert = insert;
    old_end = end;
    update_backspace((GetAsyncKeyState(VK_BACK) & 0x8000) != 0);
    update_fov((GetAsyncKeyState(VK_UP) & 0x8000) != 0, (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0);
    POINT cursor{};
    const bool cursor_valid = !menu_open || GetCursorPos(&cursor);
    if (menu_open && cursor_valid)
    {
        mouse_x = static_cast<float>(cursor.x - overlay::origin.x);
        mouse_y = static_cast<float>(cursor.y - overlay::origin.y);
    }
    update_mouse((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    if (!menu_open)
    {
        dragging = false;
        clicked = false;
        binding = false;
        active_slider = nullptr;
        return;
    }
    if (!cursor_valid)
        clicked = false;
    if (binding)
    {
        for (int key = 1; key < 255; ++key)
        {
            const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
            if (down && !binding_keys[key])
            {
                if (key != VK_ESCAPE && key != VK_INSERT && key != VK_END && key != VK_BACK && key != VK_UP && key != VK_DOWN)
                    config.aimbot.key = key;
                binding = false;
                clicked = false;
                break;
            }
            binding_keys[key] = down;
        }
    }
    if (clicked && inside(x, y, width, 34))
    {
        dragging = true;
        drag_x = mouse_x - x;
        drag_y = mouse_y - y;
        clicked = false;
    }
    if (!held)
    {
        dragging = false;
        active_slider = nullptr;
    }
    if (dragging)
    {
        x = mouse_x - drag_x;
        y = mouse_y - drag_y;
    }
    x = std::clamp(x, 0.f, std::max(0.f, screen_width - width));
    y = std::clamp(y, 0.f, std::max(0.f, screen_height - height));
}

void menu::draw()
{
    if (!menu_open)
        return;
    if (!held)
        active_slider = nullptr;
    rect(x, y, width, height, color(config.colors.menu_fill));
    rect(x, y, width, height, color(config.colors.menu_accent), false);
    text(x + 18, y + 9, "X-Engine", color(config.colors.menu_accent), 17);
    text(x + 450, y + 12, "Insert to close", muted, 12);
    const char* tabs[]{"Aim", "Players", "World", "Mortar", "Effects", "Colors", "Settings"};
    for (int i = 0; i < 7; ++i)
        if (button(x + 18 + i * 90, y + 46, 86, tabs[i], i == tab))
            tab = i;
    auto& e = config.esp;
    auto& a = config.aimbot;
    const char* bones[]{"Head", "Neck", "Chest", "Pelvis", "Nearest"};
    const char* labels[]{"Both above", "Both below", "Distance", "Name", "Off"};
    const char* modes[]{"Distance", "Crosshair", "Health"};
    const char* boxes[]{"Corners", "Full"};
    switch (tab)
    {
    case 0:
        toggle(0, 0, "Enabled", a.enabled);
        toggle(0, 1, "Draw FOV", a.draw_fov);
        toggle(0, 2, "Visibility check", a.visible_check);
        toggle(0, 3, "Team check", a.team_check);
        choice(0, 4, "Target bone", a.bone, bones, 5);
        choice(0, 5, "Target priority", a.mode, modes, 3);
        number(0, 6, "FOV (Up/Down)", a.fov, 5, 1, 800);
        integer(0, 7, "Aim speed", a.smooth, 1, 50);
        text(x + 18, y + 331, "Aim key", color(config.colors.menu_text));
        {
            char key[24] = "Mouse 1";
            if (a.key != VK_LBUTTON)
                snprintf(key, sizeof(key), "VK 0x%02X", a.key);
            if (button(x + 182, y + 328, 128, binding ? "Press key..." : key))
            {
                binding = true;
                for (int i = 1; i < 255; ++i)
                    binding_keys[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
            }
        }
        toggle(1, 0, "Silent aim", a.silent_aim);
        toggle(1, 1, "Magic bullet", a.magic_bullet);
        number(1, 2, "Release delay (s)", a.magic_bullet_delay_off, 0.1f, 0, 10, "%.1f");
        toggle(1, 3, "No recoil", config.extra.no_recoil);
        toggle(1, 4, "Prediction", config.prediction.enabled);
        toggle(1, 5, "Bullet drop", config.prediction.bullet_drop);
        toggle(1, 6, "Velocity lead", config.prediction.velocity_lead);
        toggle(1, 7, "Prediction line", config.prediction.show_line);
        break;
    case 1:
        toggle(0, 0, "Player ESP", e.enabled);
        toggle(0, 1, "Names", e.agent_name);
        toggle(0, 2, "Skeleton", e.skeleton);
        toggle(0, 3, "Boxes", e.box);
        choice(0, 4, "Box style", e.box_style, boxes, 2);
        toggle(0, 5, "Snaplines", e.lines);
        toggle(0, 6, "Health", e.health);
        toggle(0, 7, "Distance", e.distance);
        toggle(1, 0, "Visibility colors", e.visible_check);
        toggle(1, 1, "Show teammates", e.team);
        number(1, 4, "Player range (m)", e.player_distance, 50, 1, 2000);
        number(1, 5, "Skeleton range (m)", e.skeleton_distance, 25, 1, 500);
        {
            int mode = config.extra.player_text;
            choice(1, 7, "Player text", mode, labels, 5);
            if (mode != config.extra.player_text)
                set_player_text(mode);
        }
        break;
    case 2:
        toggle(0, 0, "Vehicles", e.vehicles);
        number(0, 1, "Vehicle range (m)", e.vehicle_distance, 100, 1, 5000);
        toggle(0, 3, "Dropped items", e.loot);
        number(0, 4, "Item range (m)", e.loot_distance, 10, 1, 2000);
        toggle(0, 5, "Explosives", config.extra.explosives);
        number(0, 6, "Explosive range", config.extra.explosive_range, 25, 10, 1000);
        toggle(0, 7, "Death bag info", config.extra.death_bags);
        number(0, 8, "Bag range (m)", config.extra.bag_range, 25, 10, 1000);
        toggle(1, 7, "Facing arrows", config.extra.radar_directions);
        toggle(1, 9, "Radar teammates", config.extra.radar_team);
        toggle(1, 10, "Smooth radar yaw", config.extra.radar_smoothing);
        toggle(1, 0, "Radar", e.minimap);
        toggle(1, 1, "Automatic range", e.minimap_auto_range);
        number(1, 2, "Radar range (m)", e.minimap_range, 25, 25, 5000);
        number(1, 3, "Size (pixels)", e.minimap_size, 20, 120, 500);
        number(1, 4, "Opacity", e.minimap_opacity, 0.1f, 0.1f, 1, "%.1f");
        {
            const float half = std::min(e.minimap_size, static_cast<float>(std::min(screen_width, screen_height))) * 0.5f;
            slider(1, 5, "Radar X", e.minimap_x, half, screen_width - half, screen_width - half - 12);
            slider(1, 6, "Radar Y", e.minimap_y, half, screen_height - half, half + 12);
        }
        if (button(x + 334, y + 328, 292, "Reset radar position"))
            e.minimap_x = e.minimap_y = -1;
        break;
    case 3:
        toggle(0, 0, "Mortar aim", config.mortar.mortar_aim);
        number(0, 1, "FOV (degrees)", config.mortar.fov, 5, 1, 180);
        {
            const char* modes[]{"Auto aim", "Impact radar"};
            choice(0, 3, "Main mode", config.extra.mortar_mode, modes, 2);
        }
        text(x + 18, y + 298, "Impact radar: aim the mortar manually.", muted);
        toggle(1, 0, "Automatic flares", config.anti_sam.auto_flare);
        toggle(1, 1, "SAM warning", config.anti_sam.flare_warning);
        text(x + 18, y + 268, "Page Up / Page Down: change mortar target", muted);
        break;
    case 4:
    {
        const char* styles[]{"Rainbow trail", "Rainbow shots", "Solid", "Gradient"};
        toggle(0, 0, "Bullet tracers", config.extra.tracers);
        choice(0, 1, "Tracer color", config.extra.tracer_style, styles, 4);
        number(0, 2, "Lifetime (s)", config.extra.tracer_lifetime, 0.25f, 0.25f, 5, "%.2f");
        number(0, 3, "Line width", config.extra.tracer_width, 0.5f, 1, 4, "%.1f");
        number(0, 5, "Radar arrow size", config.extra.radar_arrow_size, 1, 3, 10);
        toggle(1, 0, "Auto join Manticore", config.extra.auto_join);
        toggle(1, 1, "Anti AFK", config.extra.anti_afk);
        toggle(1, 2, "Active features HUD", config.extra.feature_hud);
        toggle(1, 3, "Silent Build X", config.extra.build_x);
        text(x + 18, y + 328, "Tracer colors: Colors > Effects", muted);
        break;
    }
    case 5:
    {
        static int group, selected;
        const char* groups[]{"Players", "World", "Effects", "Radar", "Menu"};
        const int previous = group;
        choice(0, 0, "Category", group, groups, 5);
        if (previous != group)
            selected = 0;
        auto& c = config.colors;
        struct Entry
        {
            const char* name;
            float* value;
        };
        Entry players[]{{"Visible box", e.visible_color}, {"Hidden box", e.not_visible_color}, {"Team", c.team}, {"Dead", c.dead}, {"Selected", c.selected}, {"Visible bones", c.skeleton_visible}, {"Hidden bones", c.skeleton_hidden}, {"Visible text", c.name_visible}, {"Hidden text", c.name_hidden}, {"Full health", c.health_full}, {"Low health", c.health_low}};
        Entry world[]{{"Vehicles", e.vehicle_color}, {"Items", e.loot_color}, {"SAM vehicles", c.sam}, {"Explosives", config.extra.explosive_color}, {"Death bags", config.extra.bag_color}};
        Entry effects[]{{"Mortar", c.mortar}, {"Prediction", c.prediction}, {"Aim FOV", c.fov}, {"SAM warning", c.warning}, {"Box shading", c.box_fill}, {"Label shading", c.label_fill}, {"Line glow", c.glow}, {"Tracer start", config.extra.tracer_color}, {"Tracer end", config.extra.tracer_end_color}};
        Entry radar[]{{"Background", c.radar_fill}, {"Grid", c.radar_grid}, {"Border", c.radar_border}, {"Local marker", c.radar_local}};
        Entry menu[]{{"Accent", c.menu_accent}, {"Background", c.menu_fill}, {"Labels", c.menu_text}, {"Values", c.menu_value}};
        Entry* entries[]{players, world, effects, radar, menu};
        const int counts[]{11, 5, 9, 4, 4};
        text(x + 18, y + 121, "Color", color(c.menu_text));
        if (button(x + 146, y + 118, 164, entries[group][selected].name))
            selected = (selected + 1) % counts[group];
        float* value = entries[group][selected].value;
        slider(0, 3, "Red", value[0], 0, 1, 0, "%.2f");
        slider(0, 4, "Green", value[1], 0, 1, 0, "%.2f");
        slider(0, 5, "Blue", value[2], 0, 1, 0, "%.2f");
        slider(0, 6, "Alpha", value[3], 0, 1, 0, "%.2f");
        rect(x + 352, y + 150, 236, 110, {0.15f, 0.15f, 0.15f, 1});
        rect(x + 352, y + 150, 118, 55, {0.3f, 0.3f, 0.3f, 1});
        rect(x + 470, y + 205, 118, 55, {0.3f, 0.3f, 0.3f, 1});
        rect(x + 352, y + 150, 236, 110, color(value));
        text(x + 352, y + 272, "Drag a channel to change its value.", muted, 12);
        if (button(x + 352, y + 328, 236, "Reset all colors"))
        {
            const Config defaults;
            c = defaults.colors;
            memcpy(config.extra.tracer_color, defaults.extra.tracer_color, sizeof(config.extra.tracer_color));
            memcpy(config.extra.tracer_end_color, defaults.extra.tracer_end_color, sizeof(config.extra.tracer_end_color));
            memcpy(config.extra.explosive_color, defaults.extra.explosive_color, sizeof(config.extra.explosive_color));
            memcpy(config.extra.bag_color, defaults.extra.bag_color, sizeof(config.extra.bag_color));
            memcpy(e.visible_color, defaults.esp.visible_color, sizeof(e.visible_color));
            memcpy(e.not_visible_color, defaults.esp.not_visible_color, sizeof(e.not_visible_color));
            memcpy(e.vehicle_color, defaults.esp.vehicle_color, sizeof(e.vehicle_color));
            memcpy(e.loot_color, defaults.esp.loot_color, sizeof(e.loot_color));
        }
    }
    break;
    case 6:
        if (button(x + 18, y + 88, 292, "Save settings"))
            status = save_config() ? "Settings saved" : "Save failed";
        if (button(x + 18, y + 128, 292, "Load settings"))
            status = load_config() ? "Settings loaded" : "No compatible saved settings";
        if (button(x + 18, y + 168, 292, "Restore defaults"))
        {
            config = {};
            status = "Defaults restored; save to keep them";
        }
        if (button(x + 334, y + 88, 292, "Stop"))
            stop = true;
        if (button(x + 334, y + 128, 292, "Copy settings"))
            status = copy_config() ? "Settings copied to clipboard" : "Clipboard unavailable";
        if (button(x + 334, y + 168, 292, "Paste settings"))
            status = paste_config() ? "Settings imported; save to keep them" : "Invalid settings or clipboard unavailable";
        text(x + 18, y + 244, "Settings are saved only when you press Save.", muted);
        text(x + 18, y + 274, "Stop releases drawing resources. The DLL stays loaded.", muted);
        break;
    }
    text(x + 18, y + height - 27, status, muted, 12);
    line(mouse_x - 5, mouse_y, mouse_x + 5, mouse_y, white);
    line(mouse_x, mouse_y - 5, mouse_x, mouse_y + 5, white);
    clicked = false;
}

#ifdef WD_TEST
void menu::test_fov(bool up, bool down)
{
    update_fov(up, down);
}

void menu::test_backspace(bool down)
{
    update_backspace(down);
}

void menu::test_input(int selected_tab, float a, float b, bool click, bool down)
{
    tab = selected_tab;
    click_x = mouse_x = a;
    click_y = mouse_y = b;
    clicked = click;
    old_mouse = held = click || down;
    menu_open = true;
}

void menu::test_mouse(float a, float b, bool down)
{
    mouse_x = a;
    mouse_y = b;
    update_mouse(down);
}

int menu::test_tab()
{
    return tab;
}
#endif
