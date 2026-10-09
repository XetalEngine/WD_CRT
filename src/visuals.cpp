#include "visuals.h"
#include "config.h"
#include "overlay.h"
#include "visual_math.h"
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

    void player(const game::ProjectedPlayer& p, std::uintptr_t selected, const visual_math::Projection* projection)
    {
        const auto& e = config.esp;
        const auto& data = p.player;
        if (p.is_vehicle || data.distance > e.player_distance || (data.is_in_team && !e.team))
            return;
        const auto screen = projection ? projection->project(data.world_pos) : p.screen;
        if (!screen.valid || screen.x < 0 || screen.y < 0 || screen.x >= screen_width || screen.y >= screen_height)
            return;
        game::ScreenPoint points[BONE_COUNT]{};
        const auto* projected_bones = p.bones;
        if (projection)
        {
            projected_bones = points;
            if (data.has_bones)
                for (int i = 0; i < BONE_COUNT; ++i)
                {
                    const auto& bone = data.bones[i];
                    if (bone.X != 0 || bone.Y != 0 || bone.Z != 0)
                        points[i] = projection->project(bone);
                }
        }
        float height = std::clamp(4200.f / std::max(data.distance, 1.f), 10.f, 180.f);
        float center = screen.x, top = screen.y - height;
        const auto& head = projected_bones[BONE_HEAD];
        const auto& root = projected_bones[BONE_ROOT];
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
            outlined_line(screen_width * 0.5f, static_cast<float>(screen_height), screen.x, screen.y, c);
        if (e.box)
            box(left, top, width, height, c);
        if (e.skeleton && std::isfinite(data.health) && data.health > 0 && data.has_bones && !data.is_in_vehicle && data.distance <= e.skeleton_distance)
        {
            const float ratio = data.distance / std::max(e.skeleton_distance, 1.f);
            Color skeleton = targeted ? c : player_color(data, palette.skeleton_visible, palette.skeleton_hidden);
            skeleton.a *= std::clamp(1 - ratio, 0.4f, 1.f);
            for (const auto& pair : bones)
            {
                const auto& a = projected_bones[pair[0]];
                const auto& b = projected_bones[pair[1]];
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
        const int mode = config.extra.player_text;
        const bool show_name = e.agent_name && mode != 2 && mode != 4;
        const bool show_distance = e.distance && mode != 3 && mode != 4;
        if (show_name || show_distance)
        {
            wchar_t label[32];
            const wchar_t* player_name = data.player_name[0] ? data.player_name : L"Player";
            const float label_x = head.valid ? head.x : center;
            const float label_y = (head.valid ? head.y : top) - 21;
            if (show_distance)
                swprintf_s(label, show_name ? L"[%.0fm] " : L"[%.0fm]", data.distance);
            if (show_name && show_distance)
                text_pair(label_x, label_y, label, player_name, name, 13, background);
            else
                text(label_x, label_y, show_name ? player_name : label, name, 13, true, background);
        }
    }

    void world_actor(const game::ProjectedWorldActor& actor, Color c, const visual_math::Projection* projection)
    {
        const auto screen = projection ? projection->project(actor.actor.world_position) : actor.screen;
        if (!screen.valid || screen.x < 0 || screen.y < 0 || screen.x >= screen_width || screen.y >= screen_height)
            return;
        char label[96];
        snprintf(label, sizeof(label), "%s  %.0fm", actor.actor.label, actor.actor.distance_meters);
        text(screen.x, screen.y + 7, label, c, 12.5f, true, color(config.colors.label_fill));
    }

    Color rainbow(float hue, float alpha)
    {
        hue = hue - std::floor(hue);
        const float h = hue * 6, f = h - std::floor(h);
        switch (static_cast<int>(h))
        {
        case 0:
            return {1, f, 0.15f, alpha};
        case 1:
            return {1 - f, 1, 0.15f, alpha};
        case 2:
            return {0.15f, 1, f, alpha};
        case 3:
            return {0.15f, 1 - f, 1, alpha};
        case 4:
            return {f, 0.15f, 1, alpha};
        default:
            return {1, 0.15f, 1 - f, alpha};
        }
    }

    void arrow(float x, float y, float angle, float size, Color c)
    {
        const float dx = std::sin(angle), dy = -std::cos(angle);
        const float tip_x = x + dx * size, tip_y = y + dy * size;
        line(tip_x, tip_y, x - dx * size * 0.6f - dy * size * 0.7f, y - dy * size * 0.6f + dx * size * 0.7f, c, 1.5f);
        line(tip_x, tip_y, x - dx * size * 0.6f + dy * size * 0.7f, y - dy * size * 0.6f - dx * size * 0.7f, c, 1.5f);
    }

    void effects(const game::Snapshot& snapshot, const visual_math::Projection& projection)
    {
        const auto& x = config.extra;
        if (x.tracers)
            for (const auto& trail : snapshot.trails)
            {
                game::ScreenPoint previous;
                for (int i = 0; i < trail.count && i < trail.capacity; ++i)
                {
                    const auto point = projection.project(trail.points[i]);
                    if (i && point.valid && previous.valid)
                    {
                        const float progress = static_cast<float>(i) / std::max(trail.count - 1, 1);
                        Color c = color(x.tracer_color);
                        if (x.tracer_style < 2)
                            c = rainbow(trail.hue + (x.tracer_style == 0 ? progress * 0.8f : 0.f), c.a);
                        else if (x.tracer_style == 3)
                        {
                            c.r += (x.tracer_end_color[0] - c.r) * progress;
                            c.g += (x.tracer_end_color[1] - c.g) * progress;
                            c.b += (x.tracer_end_color[2] - c.b) * progress;
                        }
                        c.a *= trail.alpha;
                        line(previous.x, previous.y, point.x, point.y, c, x.tracer_width);
                    }
                    previous = point;
                }
            }
        for (const auto& marker : snapshot.markers)
        {
            if (marker.bag ? !x.death_bags || marker.distance > x.bag_range : !x.explosives || marker.distance > x.explosive_range)
                continue;
            const auto point = projection.project(marker.world);
            if (!point.valid || point.x < 0 || point.y < 0 || point.x > screen_width || point.y > screen_height)
                continue;
            Color c = color(marker.bag ? x.bag_color : x.explosive_color);
            if (marker.bag && marker.nearby)
                c = color(config.colors.warning);
            char label[96];
            snprintf(label, sizeof(label), "%s  %.0fm", marker.label, marker.distance);
            text(point.x, point.y, label, c, 12.5f, true, color(config.colors.label_fill));
            if (marker.bag)
            {
                if (marker.nearby)
                {
                    snprintf(label, sizeof(label), "%d ON LOOT", marker.nearby);
                    text(point.x, point.y + 16, label, c, 12, true);
                }
            }
            else
            {
                if (marker.timed)
                {
                    snprintf(label, sizeof(label), "%.1fs", marker.fuse_left);
                    rect(point.x - 20, point.y + 33, 40, 3, {0, 0, 0, 0.8f});
                    rect(point.x - 20, point.y + 33, 40 * std::clamp(marker.fuse_left / marker.fuse_total, 0.f, 1.f), 3, c);
                }
                else
                    strcpy_s(label, "ARMED");
                text(point.x, point.y + 16, label, c, 12, true);
            }
        }
    }

    void mortar_radar(const game::Snapshot& snapshot)
    {
        const auto& m = snapshot.mortar;
        const float radius = 130, x = screen_width * 0.5f, y = 182;
        const float range = std::clamp(m.max_range_m, 100.f, 5000.f);
        const float yaw = m.dbg_cur_yaw * 0.0174532925f;
        const float cosine = std::cos(yaw), sine = std::sin(yaw), scale = (radius - 8) / (range * 100);
        circle(x, y, radius, color(config.colors.radar_fill), true);
        circle(x, y, radius, color(config.colors.mortar));
        for (float f : {0.25f, 0.5f, 0.75f})
            circle(x, y, radius * f, color(config.colors.radar_grid));
        line(x - radius, y, x + radius, y, color(config.colors.radar_grid));
        line(x, y - radius, x, y + radius, color(config.colors.radar_grid));
        const auto point = [&](const FVector& world, Color c, float size)
        {
            const auto d = world - m.weapon_world_pos;
            if (!visual_math::finite(d) || std::hypot(d.X, d.Y) > range * 100)
                return;
            circle(x + static_cast<float>(d.Y * cosine - d.X * sine) * scale, y - static_cast<float>(d.X * cosine + d.Y * sine) * scale, size, c, true);
        };
        for (const auto& p : snapshot.players)
            if (!p.is_vehicle && p.player.health > 0 && (!p.player.is_in_team || config.extra.radar_team))
                point(p.player.world_pos, player_color(p.player, config.esp.visible_color, config.esp.not_visible_color), 3);
        for (const auto& v : snapshot.vehicles)
            point(v.actor.world_position, vehicle_color(v), 4);
        if (std::isfinite(m.range_m) && m.range_m > 0 && m.range_m <= range)
        {
            const float impact_y = y - m.range_m * 100 * scale;
            const float spread = std::clamp(2500 * scale, 5.f, 28.f);
            circle(x, impact_y, spread, {1, 0.1f, 0.1f, 0.25f}, true);
            circle(x, impact_y, spread, {1, 0.1f, 0.1f, 1});
        }
        arrow(x, y, 0, 6, color(config.colors.radar_local));
        char label[64];
        snprintf(label, sizeof(label), "Impact %.0fm | %.0f mil", m.range_m, m.sight_mils);
        text(x, y + radius + 6, label, color(config.colors.mortar), 13, true);
    }

    void feature_hud(const game::Snapshot& snapshot)
    {
        float y = 18;
        const auto row = [&](const char* label)
        {
            text(18, y, label, color(config.colors.menu_accent), 13, false, color(config.colors.label_fill));
            y += 18;
        };
        if (config.aimbot.enabled)
            row(config.aimbot.bone == 4 ? "Aim | Nearest bone" : "Aim");
        if (config.aimbot.silent_aim)
            row("Silent aim");
        if (config.aimbot.magic_bullet)
            row("Magic Bullet");
        if (config.prediction.enabled)
            row("Prediction");
        if (config.extra.no_recoil)
            row("No recoil");
        if (config.extra.explosives)
            row("Explosives");
        if (config.extra.death_bags)
            row("d-bag");
        if (config.extra.tracers)
            row("Tracers");
        if (config.extra.build_x)
            row("Silent Build X");
        if (config.extra.auto_join)
            row("Auto join Manticore");
        if (config.extra.anti_afk)
            row("Anti AFK");
        if (snapshot.mortar.valid)
            row(config.extra.mortar_mode == 1 ? "Mortar | Impact radar" : "Mortar | Auto aim");
    }

    void radar(const game::Snapshot& snapshot)
    {
        const auto& e = config.esp;
        const float half = std::min(e.minimap_size, static_cast<float>(std::min(screen_width, screen_height))) * 0.5f;
        const float x = std::clamp(e.minimap_x < 0 ? screen_width - half - 12 : e.minimap_x, half, screen_width - half);
        const float y = std::clamp(e.minimap_y < 0 ? half + 12 : e.minimap_y, half, screen_height - half);
        const auto show_player = [&](const game::ProjectedPlayer& p)
        {
            return !p.is_vehicle && std::isfinite(p.player.health) && (p.player.health > 0 || config.radar.downed) && (p.player.is_in_team ? config.extra.radar_team : config.radar.enemies);
        };
        const auto show_marker = [&](const game::Snapshot::Marker& marker)
        { return marker.bag ? config.radar.bags : config.radar.explosives; };
        float range = e.minimap_range;
        if (e.minimap_auto_range)
        {
            range = 50;
            const auto include = [&](float distance)
            { if (std::isfinite(distance) && distance <= 5000) range = std::max(range, distance * 1.2f); };
            for (const auto& p : snapshot.players)
                if (show_player(p))
                    include(p.player.distance);
            for (const auto& v : snapshot.vehicles)
                if (radar_vehicle_visible(config, v.actor.kind))
                    include(v.actor.distance_meters);
            if (config.radar.items)
                for (const auto& item : snapshot.dropped_items)
                    include(item.actor.distance_meters);
            for (const auto& marker : snapshot.markers)
                if (show_marker(marker))
                    include(marker.distance);
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
        static float smoothed_yaw = 0;
        static double last_time = -1;
        static std::uintptr_t last_world = 0;
        const float current_yaw = static_cast<float>(snapshot.camera.rotation.Yaw);
        if (!config.extra.radar_smoothing || last_world != snapshot.world || last_time < 0 || snapshot.time < last_time || snapshot.time - last_time > 0.25)
            smoothed_yaw = current_yaw;
        else if (snapshot.time != last_time)
        {
            const float delta = std::remainder(current_yaw - smoothed_yaw, 360.f);
            smoothed_yaw += delta * (1 - std::exp(-25.f * static_cast<float>(snapshot.time - last_time)));
        }
        last_world = snapshot.world;
        last_time = snapshot.time;
        const float yaw = smoothed_yaw * 0.0174532925f;
        const float cosine = std::cos(yaw), sine = std::sin(yaw);
        const auto point = [&](const FVector& position, Color c, float radius, const Player* player = nullptr)
        {
            const float dx = static_cast<float>(position.X - snapshot.camera.location.X);
            const float dy = static_cast<float>(position.Y - snapshot.camera.location.Y);
            const float distance = std::hypot(dx, dy);
            if (!std::isfinite(distance) || distance > range * 100)
                return;
            const float inset = config.extra.radar_directions ? std::max(7.f, config.extra.radar_arrow_size + 2) : 7.f;
            const float scale = (half - inset) / (range * 100);
            const float px = x + (dy * cosine - dx * sine) * scale, py = y - (dx * cosine + dy * sine) * scale;
            if (player && player->yaw_valid && config.extra.radar_directions)
                arrow(px, py, (player->yaw - smoothed_yaw) * 0.0174532925f, config.extra.radar_arrow_size, c);
            else
                circle(px, py, radius, c, true);
        };
        for (const auto& p : snapshot.players)
            if (show_player(p))
                point(p.player.world_pos, player_color(p.player, e.visible_color, e.not_visible_color), 3, &p.player);
        for (const auto& v : snapshot.vehicles)
            if (radar_vehicle_visible(config, v.actor.kind))
                point(v.actor.world_position, vehicle_color(v), 4);
        if (config.radar.items)
            for (const auto& item : snapshot.dropped_items)
                point(item.actor.world_position, color(e.loot_color), 2);
        for (const auto& marker : snapshot.markers)
            if (show_marker(marker))
                point(marker.world, color(marker.bag ? config.extra.bag_color : config.extra.explosive_color), 3);
        circle(x, y, 3, local, true);
        const float heading = snapshot.local_yaw_valid ? (snapshot.local_yaw - smoothed_yaw) * 0.0174532925f : 0;
        line(x, y, x + std::sin(heading) * 12, y - std::cos(heading) * 12, local, 2);
        char label[32];
        snprintf(label, sizeof(label), "%.0fm", range);
        text(x, y + half - 23, label, local, 12, true);
    }
} // namespace

void visuals::draw(const game::Snapshot& snapshot, const CameraIPC* camera)
{
    if (!snapshot.valid)
        return;
    const visual_math::Projection projection(camera ? *camera : snapshot.camera);
    const auto* fresh = camera ? &projection : nullptr;
    effects(snapshot, projection);
    if (config.extra.feature_hud)
        feature_hud(snapshot);
    const Color mortar_color = color(config.colors.mortar), warning_color = color(config.colors.warning);
    const Color prediction = color(config.colors.prediction);
    const auto selected = snapshot.mortar.valid && config.mortar.mortar_aim ? snapshot.mortar.selected_actor : snapshot.aim_selected_actor;
    if (config.esp.enabled)
        for (const auto& p : snapshot.players)
            player(p, selected, fresh);
    if (config.esp.vehicles)
        for (const auto& v : snapshot.vehicles)
            if (v.actor.distance_meters <= config.esp.vehicle_distance)
                world_actor(v, vehicle_color(v), fresh);
    if (config.esp.loot)
        for (const auto& item : snapshot.dropped_items)
        {
            if (item.actor.distance_meters > config.esp.loot_distance)
                continue;
            const bool enriched = config.extra.death_bags && std::any_of(snapshot.markers.begin(), snapshot.markers.end(), [&](const auto& marker)
                                                                         { return marker.bag && marker.distance <= config.extra.bag_range && marker.actor == item.actor.address; });
            if (!enriched)
                world_actor(item, color(config.esp.loot_color), fresh);
        }
    if (config.esp.minimap)
        radar(snapshot);
    if (snapshot.prediction_line.valid && config.prediction.enabled && config.prediction.show_line)
    {
        const auto& p = snapshot.prediction_line;
        const auto bone = fresh ? projection.project(p.bone_world) : p.bone;
        const auto aim = fresh ? projection.project(p.aim_world) : p.aim;
        if (bone.valid && aim.valid)
        {
            outlined_line(bone.x, bone.y, aim.x, aim.y, prediction);
            circle(aim.x, aim.y, 3, prediction);
        }
    }
    if (snapshot.mortar.valid && config.extra.mortar_mode == 1)
        mortar_radar(snapshot);
    else if (snapshot.mortar.valid)
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
