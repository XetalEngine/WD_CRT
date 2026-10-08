#include "visuals.h"
#include "config.h"
#include "overlay.h"
#include <cmath>

namespace
{
    using namespace overlay;
    constexpr int bones[][2]{{0, 1}, {1, 2}, {2, 3}, {16, 17}, {16, 4}, {4, 5}, {5, 6}, {17, 7}, {7, 8}, {8, 9}, {3, 10}, {10, 11}, {11, 12}, {3, 13}, {13, 14}, {14, 15}};

    void outlined_line(float x1, float y1, float x2, float y2, Color c, float thickness = 1.f)
    {
        Color glow = color(config.colors.glow);
        glow.a *= c.a;
        line(x1 + 1.5f, y1 + 1.5f, x2 + 1.5f, y2 + 1.5f, {0, 0, 0, c.a}, thickness + 1.5f);
        line(x1 - 1, y1 - 1, x2 - 1, y2 - 1, glow, thickness + 1);
        line(x1 + 1, y1 + 1, x2 + 1, y2 + 1, glow, thickness + 1);
        line(x1, y1, x2, y2, c, thickness);
    }

    Color player_color(const Player& p, const float* visible, const float* hidden)
    {
        if (p.is_in_team)
            return color(config.colors.team);
        if (p.health <= 0)
            return color(config.colors.dead);
        return color(p.isVisible || !config.esp.visible_check ? visible : hidden);
    }

    Color vehicle_color(const game::ProjectedWorldActor& v)
    {
        return color(strcmp(v.actor.label, "Talon 9K-SAM") == 0 || strcmp(v.actor.label, "Vanguard CIWS") == 0 ? config.colors.sam : config.esp.vehicle_color);
    }

    void box(float x, float y, float w, float h, Color c)
    {
        rect(x - 2, y - 2, w + 4, h + 4, color(config.colors.box_fill));
        Color glow = color(config.colors.glow);
        glow.a *= c.a;
        if (config.esp.box_style == 1)
        {
            rect(x + 1, y + 1, w, h, {0, 0, 0, c.a * 200.f / 255}, false, 2);
            rect(x - 1, y - 1, w, h, glow, false, 1.5f);
            rect(x, y, w, h, c, false);
            return;
        }
        const float length = std::max(w, h) / 3.5f;
        for (int sx : {1, -1})
            for (int sy : {1, -1})
            {
                const float a = sx == 1 ? x : x + w, b = sy == 1 ? y : y + h;
                const auto corner = [&](float inset, Color shade, float thickness)
                {
                    line(a + sx * inset, b + sy * inset, a + sx * (length - inset), b + sy * inset, shade, thickness);
                    line(a + sx * inset, b + sy * inset, a + sx * inset, b + sy * (length - inset), shade, thickness);
                };
                corner(-1, {0, 0, 0, c.a}, 3);
                corner(0, c, 1);
                corner(-2, {1, 1, 1, glow.a}, 2);
                corner(1, {0, 0, 0, c.a * 100.f / 255}, 0.5f);
            }
    }

    void player(const game::ProjectedPlayer& p, std::uintptr_t selected)
    {
        const auto& e = config.esp;
        const auto& data = p.player;
        if (p.is_vehicle || !p.screen.valid || data.distance > e.player_distance || (data.is_in_team && !e.team))
            return;
        float height = std::clamp(4200.f / std::max(data.distance, 1.f), 10.f, 180.f);
        float center = p.screen.x, top = p.screen.y - height;
        const auto& head = p.bones[BONE_HEAD];
        const auto& root = p.bones[BONE_ROOT];
        const float measured = root.y - head.y;
        if (data.has_bones && !data.is_in_vehicle && head.valid && root.valid && measured >= 2 && measured <= screen_height && std::fabs(head.x - root.x) <= measured)
        {
            height = measured;
            center = head.x;
            top = head.y;
        }
        const float width = height * 0.5f;
        const float left = center - width * 0.5f, bottom = top + height;
        const auto& palette = config.colors;
        const bool targeted = selected && p.actor_addr == selected;
        const Color c = targeted ? color(palette.selected) : player_color(data, e.visible_color, e.not_visible_color);
        if (e.lines)
            outlined_line(screen_width * 0.5f, static_cast<float>(screen_height), p.screen.x, p.screen.y, c);
        if (e.box)
            box(left, top, width, height, c);
        if (e.skeleton && data.has_bones && !data.is_in_vehicle && data.distance <= e.skeleton_distance)
        {
            const float ratio = data.distance / std::max(e.skeleton_distance, 1.f);
            Color skeleton = targeted ? c : player_color(data, palette.skeleton_visible, palette.skeleton_hidden);
            skeleton.a *= std::clamp(1 - ratio, 0.4f, 1.f);
            for (const auto& pair : bones)
            {
                const auto& a = p.bones[pair[0]];
                const auto& b = p.bones[pair[1]];
                if (a.valid && b.valid && std::fabs(a.x - b.x) < height * 2 && std::fabs(a.y - b.y) < height * 2)
                    outlined_line(a.x, a.y, b.x, b.y, skeleton, std::max(1.5f, 2.5f - ratio));
            }
        }
        if (e.health && data.max_health > 0)
        {
            const float ratio = std::clamp(data.health / data.max_health, 0.f, 1.f);
            float health[4];
            for (int i = 0; i < 4; ++i)
                health[i] = palette.health_low[i] + (palette.health_full[i] - palette.health_low[i]) * ratio;
            rect(left - 9, top - 1, 5, height + 2, {0, 0, 0, health[3] * 0.8f});
            rect(left - 8, bottom - height * ratio, 3, height * ratio, color(health));
        }
        const Color name = targeted ? c : player_color(data, palette.name_visible, palette.name_hidden);
        const Color background = color(palette.label_fill);
        if (e.agent_name)
            text(center, top - 21, data.player_name[0] ? data.player_name : L"Player", name, 13, true, background);
        char label[48];
        snprintf(label, sizeof(label), "%s%s%.0fm", data.is_on_mortar ? "MORTAR " : "", data.health <= 0 ? "DEAD " : "", data.distance);
        if (e.distance)
            text(center, bottom + 5, label, name, 12, true, background);
    }

    void world_actor(const game::ProjectedWorldActor& actor, Color c)
    {
        if (!actor.screen.valid)
            return;
        char label[96];
        snprintf(label, sizeof(label), "%s  %.0fm", actor.actor.label, actor.actor.distance_meters);
        circle(actor.screen.x, actor.screen.y, 3, c);
        text(actor.screen.x, actor.screen.y + 7, label, c, 12.5f, true, color(config.colors.label_fill));
    }

    void radar(const game::Snapshot& snapshot)
    {
        const auto& e = config.esp;
        const float half = std::min(e.minimap_size, static_cast<float>(std::min(screen_width, screen_height))) * 0.5f;
        const float x = std::clamp(e.minimap_x < 0 ? screen_width - half - 12 : e.minimap_x, half, screen_width - half);
        const float y = std::clamp(e.minimap_y < 0 ? half + 12 : e.minimap_y, half, screen_height - half);
        float range = e.minimap_range;
        if (e.minimap_auto_range)
        {
            range = 50;
            for (const auto& p : snapshot.players)
                if (!p.player.is_in_team || e.team)
                    range = std::max(range, p.player.distance * 1.2f);
            for (const auto& v : snapshot.vehicles)
                range = std::max(range, v.actor.distance_meters * 1.2f);
            range = std::clamp(std::ceil(range / 25) * 25, 50.f, 5000.f);
        }
        Color background = color(config.colors.radar_fill);
        background.a *= e.minimap_opacity;
        const Color grid = color(config.colors.radar_grid), local = color(config.colors.radar_local);
        circle(x, y, half, background, true);
        circle(x, y, half - 1, color(config.colors.radar_border), false, 1.5f);
        for (float scale : {0.25f, 0.5f, 0.75f})
            circle(x, y, half * scale, grid);
        line(x - half, y, x + half, y, grid);
        line(x, y - half, x, y + half, grid);
        const float yaw = static_cast<float>(snapshot.camera.rotation.Yaw * 3.141592653589793 / 180);
        const float cosine = std::cos(yaw), sine = std::sin(yaw);
        const auto point = [&](const FVector& position, Color c, float radius)
        {
            const float dx = static_cast<float>(position.X - snapshot.camera.location.X);
            const float dy = static_cast<float>(position.Y - snapshot.camera.location.Y);
            const float distance = std::hypot(dx, dy);
            if (!std::isfinite(distance) || distance > range * 100)
                return;
            const float scale = (half - 7) / (range * 100);
            circle(x + (dy * cosine - dx * sine) * scale, y - (dx * cosine + dy * sine) * scale, radius, c, true);
        };
        for (const auto& p : snapshot.players)
            if (!p.is_vehicle && p.player.health > 0 && (!p.player.is_in_team || e.team))
                point(p.player.world_pos, player_color(p.player, e.visible_color, e.not_visible_color), 3);
        if (e.vehicles)
            for (const auto& v : snapshot.vehicles)
                point(v.actor.world_position, vehicle_color(v), 4);
        circle(x, y, 3, local, true);
        const float heading = snapshot.local_yaw_valid ? (snapshot.local_yaw - static_cast<float>(snapshot.camera.rotation.Yaw)) * 0.0174532925f : 0;
        line(x, y, x + std::sin(heading) * 12, y - std::cos(heading) * 12, local, 2);
        char label[32];
        snprintf(label, sizeof(label), "%.0fm", range);
        text(x, y + half - 23, label, local, 12, true);
    }
} // namespace

void visuals::draw(const game::Snapshot& snapshot)
{
    if (!snapshot.valid)
        return;
    const Color mortar_color = color(config.colors.mortar), warning_color = color(config.colors.warning);
    const Color prediction = color(config.colors.prediction);
    const auto selected = snapshot.mortar.valid && config.mortar.mortar_aim ? snapshot.mortar.selected_actor : snapshot.aim_selected_actor;
    if (config.esp.enabled)
        for (const auto& p : snapshot.players)
            player(p, selected);
    if (config.esp.vehicles)
        for (const auto& v : snapshot.vehicles)
            world_actor(v, vehicle_color(v));
    if (config.esp.loot)
        for (const auto& item : snapshot.dropped_items)
            world_actor(item, color(config.esp.loot_color));
    if (config.esp.minimap)
        radar(snapshot);
    if (snapshot.prediction_line.valid && config.prediction.enabled && config.prediction.show_line)
    {
        const auto& p = snapshot.prediction_line;
        outlined_line(p.bone.x, p.bone.y, p.aim.x, p.aim.y, prediction);
        circle(p.aim.x, p.aim.y, 3, prediction);
    }
    if (snapshot.mortar.valid)
    {
        const auto& m = snapshot.mortar;
        char label[96];
        snprintf(label, sizeof(label), "%.0fm    %.0f mil    Target %.0fm", m.range_m, m.sight_mils, m.target_range_m);
        text(screen_width * 0.5f, 12, label, mortar_color, 16, true);
        const int first = std::max(0, m.selected_index - 7);
        for (int i = first; i < static_cast<int>(m.candidates.size()) && i < first + 8; ++i)
        {
            const auto& c = m.candidates[i];
            wchar_t label_w[80];
            swprintf_s(label_w, L"%ls  %.0fm", c.name, c.distance_m);
            const Color label_color = i == m.selected_index ? mortar_color : c.in_range ? white
                                                                                        : warning_color;
            text(screen_width - 230.f, screen_height * 0.3f + (i - first) * 22, label_w, label_color);
        }
        for (int i = 1; i < m.sph_arc_screen_count; ++i)
            if (m.sph_arc_screen_valid[i - 1] && m.sph_arc_screen_valid[i])
                line(static_cast<float>(m.sph_arc_screen[i - 1].X), static_cast<float>(m.sph_arc_screen[i - 1].Y), static_cast<float>(m.sph_arc_screen[i].X), static_cast<float>(m.sph_arc_screen[i].Y), mortar_color, 2);
        if (m.sph_landing_screen_valid)
            circle(static_cast<float>(m.sph_landing_screen.X), static_cast<float>(m.sph_landing_screen.Y), 6, mortar_color);
        if (m.sph_marker_screen_valid)
            circle(static_cast<float>(m.sph_marker_screen.X), static_cast<float>(m.sph_marker_screen.Y), std::max(m.sph_marker_screen_radius, 5.f), mortar_color);
        if (config.mortar.mortar_aim)
        {
            const float offset = screen_width * 0.5f * std::tan(std::min(config.mortar.fov, 179.f) * 0.0087266463f);
            line(screen_width * 0.5f - offset, screen_height * 0.05f, screen_width * 0.5f - offset, screen_height * 0.95f, mortar_color);
            line(screen_width * 0.5f + offset, screen_height * 0.05f, screen_width * 0.5f + offset, screen_height * 0.95f, mortar_color);
        }
    }
    else if (config.aimbot.enabled && config.aimbot.draw_fov)
        circle(screen_width * 0.5f, screen_height * 0.5f, config.aimbot.fov, color(config.colors.fov));
    if (config.anti_sam.flare_warning && snapshot.anti_sam.incoming)
    {
        char label[80];
        snprintf(label, sizeof(label), "SAM INCOMING  %.0fm%s", snapshot.anti_sam.distance_meters, snapshot.anti_sam.flare_sent ? "  FLARE DEPLOYED" : "");
        text(screen_width * 0.5f, 42, label, warning_color, 18, true);
    }
}
