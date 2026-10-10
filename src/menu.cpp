#include "menu.h"
#include "version.h"
#include "config.h"
#include "overlay.h"
#include "game_actions.h"

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
    bool clicked, held, dragging, stop;
    int* binding;
    bool old_insert, old_end, old_mouse, old_backspace;
    bool binding_keys[256]{};
    float* active_slider;
    const char* tooltip;
    const char* status = xor_text("Ins: menu   Backspace: labels   -/=: range   [/]: bones   Up/Down: FOV   End: stop");
    struct Adjustment
    {
        int decrease, increase;
        float* value;
        float step, maximum;
        const char* format;
        int direction = 0;
        ULONGLONG repeat_at = 0, visible_until = 0;
        bool blocked = false;
    } adjustments[]{
        {VK_OEM_MINUS, VK_OEM_PLUS, &config.render_distance, 50, 5000, xor_text("Render distance: %.0f m   [- / =]")},
        {VK_OEM_4, VK_OEM_6, &config.esp.skeleton_distance, 25, 500, xor_text("Skeleton distance: %.0f m   [ / ]")},
        {VK_DOWN, VK_UP, &config.aimbot.fov, 5, 800, xor_text("Aim FOV: %.0f px   [Up / Down]")}};

    void finish_binding(int key)
    {
        if (!binding)
            return;
        const int other = binding == &config.aimbot.key ? config.name_change_key : config.aimbot.key;
        if (key != VK_ESCAPE)
        {
            if (!bindable_key(key))
                status = xor_text("Reserved hotkey; binding unchanged.");
            else if (key == other)
                status = xor_text("Aim and name change must use different keys.");
            else
            {
                *binding = key;
                status = xor_text("Key bound. Save settings to keep it.");
            }
        }
        else
            status = xor_text("Key binding cancelled.");
        binding = nullptr;
        clicked = false;
    }

    void update_adjustments(unsigned keys, ULONGLONG now)
    {
        for (int i = 0; i < 3; ++i)
        {
            auto& a = adjustments[i];
            const bool decrease = (keys & (1u << (i * 2))) != 0;
            const bool increase = (keys & (2u << (i * 2))) != 0;
            const int direction = decrease == increase ? 0 : increase ? 1
                                                                      : -1;
            if (now >= a.visible_until)
                a.visible_until = 0;
            if (binding || a.blocked)
            {
                a.blocked = decrease || increase;
                a.direction = 0;
                a.visible_until = 0;
                continue;
            }
            if (direction)
            {
                a.visible_until = now + 1000;
                if (direction != a.direction)
                {
                    *a.value = std::clamp(*a.value + direction * a.step, 1.f, a.maximum);
                    a.repeat_at = now + 300;
                }
                else if (now >= a.repeat_at)
                {
                    const auto steps = std::min<ULONGLONG>(1 + (now - a.repeat_at) / 75, 4);
                    *a.value = std::clamp(*a.value + direction * a.step * static_cast<float>(steps), 1.f, a.maximum);
                    a.repeat_at = now + 75;
                }
            }
            a.direction = direction;
        }
    }

    void draw_adjustments()
    {
        int rows = 0;
        for (const auto& a : adjustments)
            rows += a.visible_until != 0;
        if (!rows)
            return;
        const float left = std::max(4.f, (screen_width - 310.f) * 0.5f);
        const float panel_height = 12.f + rows * 23.f;
        const float panel_top = std::max(4.f, screen_height - panel_height - 24.f);
        rect(left, panel_top, 310, panel_height, color(config.colors.menu_fill));
        rect(left, panel_top, 2, panel_height, color(config.colors.menu_accent));
        float top = panel_top + 6;
        for (const auto& a : adjustments)
            if (a.visible_until)
            {
                char label[80];
                snprintf(label, sizeof(label), a.format, *a.value);
                text(left + 12, top, label, color(config.colors.menu_value), 14);
                top += 23;
            }
    }

    void set_player_text(int mode)
    {
        config.extra.player_text = mode;
        config.esp.agent_name = mode == 0 || mode == 3;
        config.esp.distance = mode == 0 || mode == 2;
    }

    void update_backspace(bool down)
    {
        if (down && !old_backspace && !binding)
        {
            const int next = (config.extra.player_text + 1) % 5;
            set_player_text(next == 1 ? 2 : next);
        }
        old_backspace = down;
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

    void hint(float a, float b, float w, float h, const char* help)
    {
        if (help && mouse_x >= a && mouse_x < a + w && mouse_y >= b && mouse_y < b + h)
            tooltip = help;
    }

    bool button(float a, float b, float w, const char* label, bool selected = false, const char* help = nullptr)
    {
        hint(a, b, w, 24, help);
        const bool hover = mouse_x >= a && mouse_x < a + w && mouse_y >= b && mouse_y < b + 24;
        Color edge = color(config.colors.menu_accent);
        edge.a *= selected ? 0.55f : (hover ? 0.4f : 0.12f);
        rect(a, b, w, 24, control);
        if (selected)
        {
            Color tint = color(config.colors.menu_accent);
            tint.a *= 0.12f;
            rect(a, b, w, 24, tint);
            rect(a, b, 2, 24, color(config.colors.menu_accent));
        }
        rect(a, b, w, 24, edge, false);
        text(a + w * 0.5f, b + 3, label, selected ? color(config.colors.menu_accent) : color(config.colors.menu_value), 13, true);
        if (!clicked || !inside(a, b, w, 24))
            return false;
        clicked = false;
        return true;
    }

    void section(int column, float top, const char* label)
    {
        const float a = x + 18 + column * 316;
        Color rule = color(config.colors.menu_accent);
        rule.a *= 0.22f;
        text(a, y + top, label, muted, 11);
        line(a + 140, y + top + 7, a + 292, y + top + 7, rule);
    }

    void divider(int column, int row)
    {
        const float a = x + 18 + column * 316, b = y + 85 + row * 30;
        Color rule = color(config.colors.menu_accent);
        rule.a *= 0.16f;
        line(a, b, a + 292, b, rule);
    }

    void toggle(int column, int row, const char* label, bool& value, const char* help)
    {
        const float a = x + 18 + column * 316, b = y + 88 + row * 30;
        text(a, b + 3, label, color(config.colors.menu_text));
        if (button(a + 246, b, 46, value ? xor_text("ON") : xor_text("OFF"), value))
            value = !value;
        hint(a, b, 292, 24, help);
    }

    void number(int column, int row, const char* label, float& value, float step, float low, float high, const char* help, const char* fmt = xor_text("%.0f"))
    {
        const float a = x + 18 + column * 316, b = y + 88 + row * 30;
        text(a, b + 3, label, color(config.colors.menu_text));
        if (button(a + 164, b, 24, xor_text("-")))
            value = std::max(low, value - step);
        if (button(a + 268, b, 24, xor_text("+")))
            value = std::min(high, value + step);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), fmt, value);
        text(a + 229, b + 3, buffer, color(config.colors.menu_value), 13, true);
        hint(a, b, 292, 24, help);
    }

    void slider(int column, int row, const char* label, float& value, float low, float high, const char* help, float fallback = 0, const char* fmt = xor_text("%.0f"))
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
        hint(a, b, 292, 24, help);
    }

    void integer(int column, int row, const char* label, int& value, int low, int high, const char* help)
    {
        float number_value = static_cast<float>(value);
        number(column, row, label, number_value, 1, static_cast<float>(low), static_cast<float>(high), help);
        value = static_cast<int>(number_value);
    }

    void choice(int column, int row, const char* label, int& value, const char* const* names, int count, const char* help)
    {
        const float a = x + 18 + column * 316, b = y + 88 + row * 30;
        text(a, b + 3, label, color(config.colors.menu_text));
        if (button(a + 164, b, 128, names[value]))
            value = (value + 1) % count;
        hint(a, b, 292, 24, help);
    }

    void keybind(int row, const char* label, int& value, const char* help)
    {
        const float top = y + 88 + row * 30;
        char key[24];
        if (value >= VK_LBUTTON && value <= VK_XBUTTON2 && value != VK_CANCEL)
            snprintf(key, sizeof(key), xor_text("Mouse %d"), value > VK_CANCEL ? value - 1 : value);
        else if (value == VK_RETURN)
            strcpy_s(key, xor_text("Enter"));
        else if ((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))
            snprintf(key, sizeof(key), xor_text("%c"), value);
        else if (value >= VK_F1 && value <= VK_F24)
            snprintf(key, sizeof(key), xor_text("F%d"), value - VK_F1 + 1);
        else
            snprintf(key, sizeof(key), xor_text("VK 0x%02X"), value);
        text(x + 18, top + 3, label, color(config.colors.menu_text));
        if (button(x + 182, top, 128, binding == &value ? xor_text("Press key...") : key, false, help))
        {
            binding = &value;
            for (int i = 1; i < 255; ++i)
                binding_keys[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
        }
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
    unsigned adjustment_keys = 0;
    bool adjusting = false;
    for (int i = 0; i < 3; ++i)
    {
        const auto& a = adjustments[i];
        if (GetAsyncKeyState(a.decrease) & 0x8000)
            adjustment_keys |= 1u << (i * 2);
        if (GetAsyncKeyState(a.increase) & 0x8000)
            adjustment_keys |= 2u << (i * 2);
        adjusting |= a.visible_until != 0 || a.blocked;
    }
    if (adjustment_keys || adjusting)
        update_adjustments(adjustment_keys, GetTickCount64());
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
        binding = nullptr;
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
                finish_binding(key);
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
    tooltip = nullptr;
    if (!menu_open)
    {
        draw_adjustments();
        return;
    }
    if (!held)
        active_slider = nullptr;
    Color border = color(config.colors.menu_accent);
    border.a *= 0.3f;
    rect(x, y, width, height, color(config.colors.menu_fill));
    rect(x, y, width, height, border, false);
    rect(x, y, width, 2, color(config.colors.menu_accent));
    line(x + 18, y + 36, x + width - 18, y + 36, border);
    rect(x + 1, y + height - 34, width - 2, 33, control);
    line(x + 18, y + height - 34, x + width - 18, y + height - 34, border);
    text(x + 18, y + 9, version::title(), color(config.colors.menu_accent), 17);
    text(x + 450, y + 12, xor_text("Insert to close"), muted, 12);
    const char* tabs[]{xor_text("Aim"), xor_text("Players"), xor_text("World"), xor_text("Mini Radar"), xor_text("Mortar"), xor_text("Effects"), xor_text("Colors"), xor_text("Settings")};
    const char* tab_help[]{xor_text("Aim targeting, prediction and projectile options."), xor_text("Player boxes, names, skeletons and display ranges."), xor_text("Vehicle, item, explosive and bag labels in the world."), xor_text("Radar size, position, range and visible categories."), xor_text("Mortar targeting, impact radar and helicopter flares."), xor_text("Bullet trails, automatic actions and feature HUD."), xor_text("Pick the colors and transparency of overlay elements."), xor_text("Save, load, share or reset settings; stop the overlay.")};
    for (int i = 0; i < 8; ++i)
        if (button(x + 18 + i * 78, y + 46, 74, tabs[i], i == tab, tab_help[i]))
            tab = i;
    line(x + 322, y + 78, x + 322, y + (tab == 7 ? 208 : height - 43), border);
    const char* headings[][2]{{xor_text("TARGETING"), xor_text("PROJECTILES")}, {xor_text("PLAYER OVERLAY"), xor_text("FILTERS & RANGE")}, {xor_text("WORLD MARKERS"), xor_text("RELATED SETTINGS")}, {xor_text("RADAR SETUP"), xor_text("SHOW ON RADAR")}, {xor_text("MORTAR TARGETING"), xor_text("HELICOPTER DEFENSE")}, {xor_text("BULLET TRACERS"), xor_text("EXTRA FEATURES")}, {xor_text("PALETTE EDITOR"), xor_text("COLOR PREVIEW")}, {xor_text("LOCAL SETTINGS"), xor_text("SHARING & SESSION")}};
    section(0, 73, headings[tab][0]);
    section(1, 73, headings[tab][1]);
    auto& e = config.esp;
    auto& a = config.aimbot;
    const char* bones[]{xor_text("Head"), xor_text("Neck"), xor_text("Chest"), xor_text("Pelvis"), xor_text("Nearest")};
    const char* labels[]{xor_text("Both above"), xor_text("Distance"), xor_text("Name"), xor_text("Off")};
    const char* modes[]{xor_text("Distance"), xor_text("Crosshair"), xor_text("Health")};
    const char* boxes[]{xor_text("Corners"), xor_text("Full")};
    switch (tab)
    {
    case 0:
        divider(0, 4);
        divider(0, 6);
        divider(1, 5);
        toggle(0, 0, xor_text("Enabled"), a.enabled, xor_text("Enables aiming while you hold the aim key.\nAiming pauses while the menu is open."));
        toggle(0, 1, xor_text("Draw FOV"), a.draw_fov, xor_text("Shows the aim selection circle around the crosshair."));
        toggle(0, 2, xor_text("Visibility check"), a.visible_check, xor_text("Requires visible targets, except Magic when\nMagic ignore visibility is enabled."));
        toggle(0, 3, xor_text("Team check"), a.team_check, xor_text("Excludes teammates from aim target selection."));
        choice(0, 4, xor_text("Target bone"), a.bone, bones, 5, xor_text("Chooses the body part to aim at. Nearest selects\nthe valid bone closest to the crosshair."));
        choice(0, 5, xor_text("Target priority"), a.mode, modes, 3, xor_text("Prefers the closest target, nearest to the crosshair,\nor lowest health. Click to cycle."));
        number(0, 6, xor_text("FOV (Up/Down)"), a.fov, 5, 1, 800, xor_text("Sets the aim selection radius in screen pixels.\nHold Up / Down to adjust in 5-pixel steps."));
        integer(0, 7, xor_text("Aim speed"), a.smooth, 1, 50, xor_text("Controls how quickly normal aim turns toward a target.\nHigher values turn faster."));
        keybind(8, xor_text("Aim key"), a.key, xor_text("Click to bind aim; Escape cancels.\nUse a different key from Name change key."));
        keybind(9, xor_text("Name change key"), config.name_change_key, xor_text("Hold aim on a highlighted player, then press this key\nto copy their name. Click to rebind; Escape cancels."));
        toggle(1, 0, xor_text("Silent aim"), a.silent_aim, xor_text("Redirects each new local projectile once. Enable\nPrediction for movement lead and drop compensation."));
        toggle(1, 1, xor_text("Magic bullet"), a.magic_bullet, xor_text("Keeps steering local projectiles toward the target.\nWith Silent also ON, uses Magic min distance below."));
        toggle(1, 2, xor_text("Magic ignore visibility"), config.magic_ignore_visibility, xor_text("Lets Magic steer toward an occluded selected target.\nFOV, team and projectile validity checks still apply."));
        number(1, 3, xor_text("Release delay (s)"), a.magic_bullet_delay_off, 0.1f, 0, 10, xor_text("Waits before selecting another target after your\nMagic Bullet target reaches zero health."), xor_text("%.1f"));
        toggle(1, 4, xor_text("No recoil"), config.extra.no_recoil, xor_text("Suppresses weapon recoil while enabled.\nRestores the original values when disabled."));
        toggle(1, 5, xor_text("Prediction"), config.prediction.enabled, xor_text("Estimates where to aim for projectile travel time.\nMagic Bullet uses the current target bone instead."));
        toggle(1, 6, xor_text("Bullet drop"), config.prediction.bullet_drop, xor_text("Compensates for projectile gravity when\nprediction is enabled."));
        toggle(1, 7, xor_text("Velocity lead"), config.prediction.velocity_lead, xor_text("Aims ahead of moving targets when\nprediction is enabled."));
        toggle(1, 8, xor_text("Prediction line"), config.prediction.show_line, xor_text("Shows a muzzle-based preview of the predicted point.\nSilent solves separately for each live projectile."));
        if (a.magic_bullet && a.silent_aim)
        {
            divider(1, 9);
            slider(1, 9, xor_text("Magic min dist (m)"), config.magic_min_distance, 0, 1000, xor_text("With Silent + Magic: Silent below this target distance;\nMagic at or above it. 0 keeps Magic at every range."));
        }
        break;
    case 1:
        divider(0, 3);
        divider(0, 6);
        section(1, 187, xor_text("DISTANCE LIMITS"));
        section(1, 277, xor_text("LABEL FORMAT"));
        toggle(0, 0, xor_text("Player ESP"), e.enabled, xor_text("Shows player overlays using the options below.\nMini Radar has its own visibility switches."));
        toggle(0, 1, xor_text("Names"), e.agent_name, xor_text("Includes player names in the selected text layout."));
        toggle(0, 2, xor_text("Skeleton"), e.skeleton, xor_text("Draws bones for living players within skeleton range."));
        toggle(0, 3, xor_text("Boxes"), e.box, xor_text("Draws a box around each player within player range."));
        choice(0, 4, xor_text("Box style"), e.box_style, boxes, 2, xor_text("Switches between corner boxes and full rectangles."));
        toggle(0, 5, xor_text("Snaplines"), e.lines, xor_text("Draws lines from the bottom of the screen to players."));
        toggle(0, 6, xor_text("Health"), e.health, xor_text("Shows player health bars."));
        toggle(0, 7, xor_text("Distance"), e.distance, xor_text("Includes distance in meters in player labels."));
        toggle(1, 0, xor_text("Visibility colors"), e.visible_check, xor_text("Uses different player colors for visible and\nhidden players. Edit those colors in Colors."));
        toggle(1, 1, xor_text("Show teammates"), e.team, xor_text("Includes teammates in player overlays.\nRadar teammates are controlled in Mini Radar."));
        number(1, 4, xor_text("Player range (m)"), e.player_distance, 50, 1, 2000, xor_text("Maximum distance for player overlays in the world.\nDoes not limit the mini radar."));
        number(1, 5, xor_text("Skeleton range (m)"), e.skeleton_distance, 25, 1, 500, xor_text("Maximum distance for drawing player skeletons.\nHold [ / ] to adjust in 25-meter steps."));
        {
            // Keep saved mode IDs stable while omitting the retired feet mode.
            int mode = std::max(0, config.extra.player_text - 1);
            choice(1, 7, xor_text("Player text"), mode, labels, 4, xor_text("Cycles combined labels, distance only, name only,\nor no text. Labels stay above the head."));
            const int stored_mode = mode == 0 ? 0 : mode + 1;
            if (stored_mode != config.extra.player_text)
                set_player_text(stored_mode);
        }
        break;
    case 2:
        section(0, 157, xor_text("LOOT & HAZARDS"));
        divider(0, 5);
        divider(0, 7);
        toggle(0, 0, xor_text("Vehicles"), e.vehicles, xor_text("Shows vehicle labels and distances in the world."));
        number(0, 1, xor_text("Vehicle range (m)"), e.vehicle_distance, 100, 1, 5000, xor_text("Maximum distance for world vehicle labels\nand vehicle aim candidates."));
        toggle(0, 3, xor_text("Dropped items"), e.loot, xor_text("Shows dropped-item labels and distances in the world."));
        number(0, 4, xor_text("Item range (m)"), e.loot_distance, 10, 1, 2000, xor_text("Maximum distance for dropped-item labels."));
        toggle(0, 5, xor_text("Explosives"), config.extra.explosives, xor_text("Shows explosive labels and remaining fuse time\nwhen a timed grenade provides it."));
        number(0, 6, xor_text("Explosive range"), config.extra.explosive_range, 25, 10, 1000, xor_text("Maximum distance for world explosive labels."));
        toggle(0, 7, xor_text("d-bag info"), config.extra.death_bags, xor_text("Shows bag/container labels and nearby looter counts."));
        number(0, 8, xor_text("Bag range (m)"), config.extra.bag_range, 25, 10, 1000, xor_text("Maximum distance for bag and container labels."));
        text(x + 334, y + 94, xor_text("Mini Radar"), color(config.colors.menu_text));
        text(x + 334, y + 116, xor_text("Choose radar categories in Mini Radar.\nRadar filters are separate from these labels."), muted, 12);
        divider(1, 3);
        text(x + 334, y + 184, xor_text("Marker colors"), color(config.colors.menu_text));
        text(x + 334, y + 206, xor_text("Edit marker colors in Colors > World.\nEach category has its own color."), muted, 12);
        section(1, 277, xor_text("GLOBAL DISTANCE"));
        number(1, 7, xor_text("Render range (m)"), config.render_distance, 50, 1, 5000, xor_text("Caps all world ESP; each category keeps its own limit.\nHold - / = (top row) to adjust. Radar stays separate."));
        text(x + 334, y + 332, xor_text("The smaller of global and category range applies."), muted, 11);
        break;
    case 3:
        divider(0, 3);
        divider(0, 5);
        divider(0, 7);
        divider(0, 10);
        divider(1, 3);
        divider(1, 7);
        toggle(0, 0, xor_text("Radar"), e.minimap, xor_text("Shows the circular mini radar with selected categories."));
        toggle(0, 1, xor_text("Automatic range"), e.minimap_auto_range, xor_text("Fits the radar range to the selected categories.\nHidden categories do not expand the range."));
        number(0, 2, xor_text("Radar range (m)"), e.minimap_range, 25, 25, 5000, xor_text("Sets the radar radius in meters when\nAutomatic range is OFF."));
        number(0, 3, xor_text("Size (pixels)"), e.minimap_size, 20, 120, 500, xor_text("Sets the radar diameter in pixels.\nIts position is kept inside the screen."));
        number(0, 4, xor_text("Opacity"), e.minimap_opacity, 0.1f, 0.1f, 1, xor_text("Changes the radar background opacity.\nMarkers keep their own colors and opacity."), xor_text("%.1f"));
        {
            const float half = std::min(e.minimap_size, static_cast<float>(std::min(screen_width, screen_height))) * 0.5f;
            slider(0, 5, xor_text("Radar X"), e.minimap_x, half, screen_width - half, xor_text("Moves the radar center horizontally.\nDrag the slider to change its position."), screen_width - half - 12);
            slider(0, 6, xor_text("Radar Y"), e.minimap_y, half, screen_height - half, xor_text("Moves the radar center vertically.\nDrag the slider to change its position."), half + 12);
        }
        toggle(0, 7, xor_text("Facing arrows"), config.extra.radar_directions, xor_text("Uses arrows to show player facing direction\nwhen that information is available."));
        number(0, 8, xor_text("Arrow size"), config.extra.radar_arrow_size, 1, 3, 10, xor_text("Sets the size of player direction arrows on the radar."));
        toggle(0, 9, xor_text("Smooth radar yaw"), config.extra.radar_smoothing, xor_text("Smooths radar rotation as the camera turns.\nTurn OFF for immediate rotation."));
        if (button(x + 18, y + 388, 292, xor_text("Reset radar position"), false, xor_text("Moves the radar back to the top right\nwith a 12-pixel margin.")))
            e.minimap_x = e.minimap_y = -1;
        toggle(1, 0, xor_text("Enemies"), config.radar.enemies, xor_text("Includes enemy player markers on the radar."));
        toggle(1, 1, xor_text("Teammates"), config.extra.radar_team, xor_text("Includes teammate markers on the radar."));
        toggle(1, 2, xor_text("Include downed players"), config.radar.downed, xor_text("Also includes zero-health players from the selected\nenemy and teammate groups on the radar."));
        toggle(1, 3, xor_text("Helicopters"), config.radar.helicopters, xor_text("Includes helicopter markers on the radar."));
        toggle(1, 4, xor_text("Ground vehicles"), config.radar.ground, xor_text("Includes tanks, APCs, buggies, trucks and motorcycles\non the radar."));
        toggle(1, 5, xor_text("Boats"), config.radar.boats, xor_text("Includes boat markers on the radar."));
        toggle(1, 6, xor_text("Stationary weapons"), config.radar.stationary, xor_text("Includes stationary weapon markers on the radar."));
        toggle(1, 7, xor_text("Dropped items"), config.radar.items, xor_text("Includes dropped-item markers on the radar.\nWorld labels can remain OFF."));
        toggle(1, 8, xor_text("Explosives"), config.radar.explosives, xor_text("Includes explosive markers on the radar.\nWorld labels can remain OFF."));
        toggle(1, 9, xor_text("Bags / containers"), config.radar.bags, xor_text("Includes bag and container markers on the radar.\nWorld labels can remain OFF."));
        break;
    case 4:
        section(0, 157, xor_text("OPERATING MODE"));
        section(0, 241, xor_text("CONTROLS"));
        toggle(0, 0, xor_text("Mortar aim"), config.mortar.mortar_aim, xor_text("Enables mortar auto-targeting in Auto aim mode.\nHold the aim key while the menu is closed."));
        number(0, 1, xor_text("FOV (degrees)"), config.mortar.fov, 5, 1, 180, xor_text("Sets the angular limit for mortar target selection."));
        {
            const char* modes[]{xor_text("Auto aim"), xor_text("Impact radar")};
            choice(0, 3, xor_text("Main mode"), config.extra.mortar_mode, modes, 2, xor_text("Auto aim controls mortar targeting. Impact radar\nshows contacts and landing position for manual aim."));
        }
        text(x + 18, y + 298, xor_text("Impact radar: aim the mortar manually."), muted);
        toggle(1, 0, xor_text("Automatic flares"), config.anti_sam.auto_flare, xor_text("Releases helicopter flares when an incoming SAM\nis close enough and on an intercept course."));
        toggle(1, 1, xor_text("SAM warning"), config.anti_sam.flare_warning, xor_text("Shows an incoming SAM warning while in a helicopter."));
        text(x + 18, y + 268, xor_text("Page Up / Page Down: change mortar target"), muted);
        break;
    case 5:
    {
        divider(0, 2);
        divider(1, 3);
        const char* styles[]{xor_text("Rainbow trail"), xor_text("Rainbow shots"), xor_text("Solid"), xor_text("Gradient")};
        toggle(0, 0, xor_text("Bullet tracers"), config.extra.tracers, xor_text("Draws trails for your recent projectiles."));
        choice(0, 1, xor_text("Tracer color"), config.extra.tracer_style, styles, 4, xor_text("Cycles rainbow trails, rainbow per shot, solid color\nand a two-color gradient. Edit colors in Colors."));
        number(0, 2, xor_text("Lifetime (s)"), config.extra.tracer_lifetime, 0.25f, 0.25f, 5, xor_text("Sets how long a projectile trail stays visible."), xor_text("%.2f"));
        number(0, 3, xor_text("Line width"), config.extra.tracer_width, 0.5f, 1, 4, xor_text("Sets the thickness of projectile trails in pixels."), xor_text("%.1f"));
        const bool previous_join = config.extra.auto_join;
        toggle(1, 0, xor_text("Auto join Manticore"), config.extra.auto_join, xor_text("Requests and confirms Manticore in a live match.\nWorks on the faction/deploy screen without a pawn."));
        if (previous_join != config.extra.auto_join)
            log(xor_text("Auto faction: menu toggle %s"), config.extra.auto_join ? xor_text("ON") : xor_text("OFF"));
        if (config.extra.auto_join)
        {
            section(0, 205, xor_text("FACTION STATUS"));
            const auto faction = game_actions::faction_status();
            char line[128];
            snprintf(line, sizeof(line), xor_text("Delivery: %s"), faction.delivery);
            text(x + 18, y + 224, line, muted, 13);
            snprintf(line, sizeof(line), xor_text("Last: %s"), faction.result);
            text(x + 18, y + 246, line, muted, 13);
            hint(x + 18, y + 224, 292, 42, xor_text("Shows callback delivery and faction progress.\nRequests retry until the local faction is Manticore."));
        }
        toggle(1, 1, xor_text("Anti AFK"), config.extra.anti_afk, xor_text("Makes a tiny aim movement every five seconds on foot,\nwhile the menu is closed and aim is inactive."));
        toggle(1, 2, xor_text("Active features HUD"), config.extra.feature_hud, xor_text("Shows a compact list of enabled features\nat the top left of the screen."));
        const bool previous_build = config.extra.build_x;
        toggle(1, 3, xor_text("Silent Build X"), config.extra.build_x, xor_text("Moves nearby build markers to your aimed surface.\nRequires the hammer; restores them when disabled."));
        if (previous_build != config.extra.build_x)
            log(xor_text("Build X: menu toggle %s"), config.extra.build_x ? xor_text("ON") : xor_text("OFF"));
        if (config.extra.build_x)
        {
            section(1, 205, xor_text("BUILD X STATUS"));
            const auto build = game_actions::build_status();
            char line[128];
            snprintf(line, sizeof(line), xor_text("Delivery: %s"), build.delivery);
            text(x + 334, y + 224, line, muted, 13);
            snprintf(line, sizeof(line), xor_text("Last: %s"), build.result);
            text(x + 334, y + 246, line, muted, 13);
            hint(x + 334, y + 224, 292, 42, xor_text("Callback delivery and the last active result.\nClose the menu to run; reopen it to read the result."));
        }
        text(x + 18, y + 328, xor_text("Tracer colors: Colors > Effects"), muted);
        break;
    }
    case 6:
    {
        section(0, 157, xor_text("RGBA CHANNELS"));
        static int group, selected;
        const char* groups[]{xor_text("Players"), xor_text("World"), xor_text("Effects"), xor_text("Radar"), xor_text("Menu")};
        const int previous = group;
        choice(0, 0, xor_text("Category"), group, groups, 5, xor_text("Chooses which group of overlay colors to edit.\nClick to cycle through the groups."));
        if (previous != group)
            selected = 0;
        auto& c = config.colors;
        struct Entry
        {
            const char* name;
            float* value;
        };
        Entry players[]{{xor_text("Visible box"), e.visible_color}, {xor_text("Hidden box"), e.not_visible_color}, {xor_text("Team"), c.team}, {xor_text("Dead"), c.dead}, {xor_text("Selected hidden"), c.selected}, {xor_text("Selected visible"), config.selected_visible_color}, {xor_text("Visible bones"), c.skeleton_visible}, {xor_text("Hidden bones"), c.skeleton_hidden}, {xor_text("Visible text"), c.name_visible}, {xor_text("Hidden text"), c.name_hidden}, {xor_text("Full health"), c.health_full}, {xor_text("Low health"), c.health_low}};
        Entry world[]{{xor_text("Vehicles"), e.vehicle_color}, {xor_text("Items"), e.loot_color}, {xor_text("SAM vehicles"), c.sam}, {xor_text("Explosives"), config.extra.explosive_color}, {xor_text("d-bag"), config.extra.bag_color}};
        Entry effects[]{{xor_text("Mortar"), c.mortar}, {xor_text("Prediction"), c.prediction}, {xor_text("Aim FOV"), c.fov}, {xor_text("SAM warning"), c.warning}, {xor_text("Box shading"), c.box_fill}, {xor_text("Label shading"), c.label_fill}, {xor_text("Line glow"), c.glow}, {xor_text("Tracer start"), config.extra.tracer_color}, {xor_text("Tracer end"), config.extra.tracer_end_color}};
        Entry radar[]{{xor_text("Background"), c.radar_fill}, {xor_text("Grid"), c.radar_grid}, {xor_text("Border"), c.radar_border}, {xor_text("Local marker"), c.radar_local}};
        Entry menu[]{{xor_text("Accent"), c.menu_accent}, {xor_text("Background"), c.menu_fill}, {xor_text("Labels"), c.menu_text}, {xor_text("Values"), c.menu_value}};
        Entry* entries[]{players, world, effects, radar, menu};
        const int counts[]{12, 5, 9, 4, 4};
        text(x + 18, y + 121, xor_text("Color"), color(c.menu_text));
        if (button(x + 146, y + 118, 164, entries[group][selected].name, false, xor_text("Selects the element whose color you want to edit.\nClick to cycle; use the channel sliders below.")))
            selected = (selected + 1) % counts[group];
        float* value = entries[group][selected].value;
        slider(0, 3, xor_text("Red"), value[0], 0, 1, xor_text("Changes the red channel of the selected color."), 0, xor_text("%.2f"));
        slider(0, 4, xor_text("Green"), value[1], 0, 1, xor_text("Changes the green channel of the selected color."), 0, xor_text("%.2f"));
        slider(0, 5, xor_text("Blue"), value[2], 0, 1, xor_text("Changes the blue channel of the selected color."), 0, xor_text("%.2f"));
        slider(0, 6, xor_text("Alpha"), value[3], 0, 1, xor_text("Changes opacity of the selected color.\n0 is transparent; 1 is fully opaque."), 0, xor_text("%.2f"));
        rect(x + 352, y + 150, 236, 110, {0.15f, 0.15f, 0.15f, 1});
        rect(x + 352, y + 150, 118, 55, {0.3f, 0.3f, 0.3f, 1});
        rect(x + 470, y + 205, 118, 55, {0.3f, 0.3f, 0.3f, 1});
        rect(x + 352, y + 150, 236, 110, color(value));
        text(x + 352, y + 272, xor_text("Drag a channel to change its value."), muted, 12);
        if (button(x + 352, y + 328, 236, xor_text("Reset all colors"), false, xor_text("Restores every overlay and menu color to its default.\nOther settings stay unchanged; Save keeps the reset.")))
        {
            const Config defaults;
            c = defaults.colors;
            memcpy(config.selected_visible_color, defaults.selected_visible_color, sizeof(config.selected_visible_color));
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
    case 7:
        section(0, 218, xor_text("NOTES"));
        line(x + 18, y + 160, x + 310, y + 160, border);
        line(x + 334, y + 120, x + 626, y + 120, border);
        if (button(x + 18, y + 88, 292, xor_text("Save settings"), false, xor_text("Saves the current settings for the next time\nyou load the DLL.")))
            status = save_config() ? xor_text("Settings saved") : xor_text("Save failed");
        if (button(x + 18, y + 128, 292, xor_text("Load settings"), false, xor_text("Replaces current settings with your last saved setup.\nUnsaved changes are discarded.")))
            status = load_config() ? xor_text("Settings loaded") : xor_text("No compatible saved settings");
        if (button(x + 18, y + 168, 292, xor_text("Restore defaults"), false, xor_text("Resets all settings and colors to their defaults.\nPress Save settings to keep them.")))
        {
            config = {};
            status = xor_text("Defaults restored; save to keep them");
        }
        if (button(x + 334, y + 88, 292, xor_text("Stop"), false, xor_text("Stops updates and drawing, then releases resources.\nThe DLL stays loaded. End does the same thing.")))
            stop = true;
        if (button(x + 334, y + 128, 292, xor_text("Copy settings"), false, xor_text("Copies your current setup as text to the clipboard\nso you can share it or keep a backup.")))
            status = copy_config() ? xor_text("Settings copied to clipboard") : xor_text("Clipboard unavailable");
        if (button(x + 334, y + 168, 292, xor_text("Paste settings"), false, xor_text("Imports a valid settings string from the clipboard.\nPress Save settings to keep the imported setup.")))
            status = paste_config() ? xor_text("Settings imported; save to keep them") : xor_text("Invalid settings or clipboard unavailable");
        text(x + 18, y + 244, xor_text("Settings are saved only when you press Save."), muted);
        text(x + 18, y + 274, xor_text("Stop releases drawing resources. The DLL stays loaded."), muted);
        break;
    }
    text(x + 18, y + height - 27, status, muted, 12);
    if (tooltip && !held && !dragging && !binding)
    {
        constexpr float tip_width = 400, tip_height = 48;
        const float tx = std::clamp(mouse_x + 14, 4.f, std::max(4.f, screen_width - tip_width - 4));
        const float ty = std::clamp(mouse_y + 18 + tip_height <= screen_height - 4 ? mouse_y + 18 : mouse_y - tip_height - 10, 4.f, std::max(4.f, screen_height - tip_height - 4));
        rect(tx, ty, tip_width, tip_height, control);
        rect(tx, ty, tip_width, tip_height, color(config.colors.menu_accent), false);
        text(tx + 10, ty + 7, tooltip, white, 12);
    }
    else
        tooltip = nullptr;
    line(mouse_x - 5, mouse_y, mouse_x + 5, mouse_y, white);
    line(mouse_x, mouse_y - 5, mouse_x, mouse_y + 5, white);
    draw_adjustments();
    clicked = false;
}

#ifdef WD_TEST
void menu::test_adjust_keys(ULONGLONG now, std::initializer_list<int> keys, bool capture)
{
    unsigned mask = 0;
    for (int i = 0; i < 3; ++i)
        for (int key : keys)
        {
            if (key == adjustments[i].decrease)
                mask |= 1u << (i * 2);
            if (key == adjustments[i].increase)
                mask |= 2u << (i * 2);
        }
    const auto previous_binding = binding;
    binding = capture ? &config.aimbot.key : nullptr;
    update_adjustments(mask, now);
    binding = previous_binding;
}

bool menu::test_adjustment_visible(int index)
{
    return index >= 0 && index < 3 && adjustments[index].visible_until != 0;
}

void menu::test_bind_key(int key)
{
    finish_binding(key);
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
