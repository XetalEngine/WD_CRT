#pragma once
#include "stdafx.h"

struct AimbotSettings
{
    bool enabled = true;
    bool draw_fov = true;
    bool visible_check = true;
    bool team_check = true;
    int bone = 4; // nearest projected bone
    int mode = 0;
    int key = VK_LBUTTON;
    float fov = 80.f;
    int smooth = 15;
    bool silent_aim = false;
    bool magic_bullet = false;
    float magic_bullet_delay_off = 0.f;
};

struct PredictionSettings
{
    bool enabled = false;
    bool bullet_drop = true;
    bool velocity_lead = true;
    bool show_line = true;
};

struct AntiSamSettings
{
    bool auto_flare = true;
    bool flare_warning = true;
};

struct Config
{
    struct
    {
        bool enabled = true;
        bool agent_name = true;
        bool skeleton = true;
        bool box = true;
        int box_style = 0; // 0 = edge/corner box (default), 1 = full box
        bool lines = false;
        bool health = true;
        bool distance = true;
        bool visible_check = true;
        bool team = false;
        float player_distance = 750.f;
        float skeleton_distance = 200.f;
        float not_visible_color[4] = {1, 0, 0, 200.f / 255};
        float visible_color[4] = {0, 1, 0, 1};
        bool loot = true;
        float loot_distance = 80.f;
        float loot_color[4] = {0, 1, 0, 1};
        bool vehicles = true;
        float vehicle_distance = 2000.f;
        float vehicle_color[4] = {0.0f, 0.62f, 1.0f, 1.0f};
        bool minimap = true;
        bool minimap_auto_range = true;
        float minimap_range = 300.f;
        float minimap_size = 400.f;
        float minimap_opacity = 0.70f;
        float minimap_x = -1.f;
        float minimap_y = -1.f;
    } esp;

    AimbotSettings aimbot;
    PredictionSettings prediction;
    AntiSamSettings anti_sam;

    struct
    {
        bool mortar_aim = true;
        float fov = 30.f;
        // SPH-2 branch: 0 = direct/low, 1 = indirect/high. Explicit selection
        // avoids TargetRotation switching branches while the aim key is held.
        int arc_mode = 0;
        // SPH-2 field calibration: actual range / native nominal range. A value
        // below 1 means the spawned shell lands short of the HUD/native table.
        // Tunable live with [ and ].
        float range_scale = 0.77f;
    } mortar;

    // Appended so version 1 settings retain their original layout.
    struct
    {
        float team[4] = {0, 1, 1, 1};
        float dead[4] = {1, 200.f / 255, 0, 1};
        float selected[4] = {1, 1, 0, 1};
        float skeleton_visible[4] = {0, 1, 0, 1};
        float skeleton_hidden[4] = {1, 80.f / 255, 80.f / 255, 1};
        float name_visible[4] = {0, 1, 50.f / 255, 1};
        float name_hidden[4] = {1, 80.f / 255, 50.f / 255, 1};
        float health_full[4] = {0, 1, 50.f / 255, 1};
        float health_low[4] = {1, 80.f / 255, 50.f / 255, 1};
        float sam[4] = {1, 0, 1, 1};
        float mortar[4] = {1, 1, 0, 1};
        float prediction[4] = {0, 1, 0, 1};
        float fov[4] = {0, 1, 0, 0.5f};
        float warning[4] = {1, 0, 1, 1};
        float box_fill[4] = {19.f / 255, 19.f / 255, 39.f / 255, 138.f / 255};
        float label_fill[4] = {26.f / 255, 26.f / 255, 51.f / 255, 77.f / 255};
        float glow[4] = {150.f / 255, 150.f / 255, 1, 80.f / 255};
        float radar_fill[4] = {0, 0, 0, 1};
        float radar_grid[4] = {1, 1, 1, 25.f / 255};
        float radar_border[4] = {0, 0, 0, 220.f / 255};
        float radar_local[4] = {1, 1, 1, 1};
        float menu_accent[4] = {0, 1, 0, 1};
        float menu_fill[4] = {5.f / 255, 5.f / 255, 15.f / 255, 230.f / 255};
        float menu_text[4] = {1, 127.f / 255, 0, 1};
        float menu_value[4] = {1, 1, 0, 1};
    } colors;

    // Version 3: keep the older settings layout intact.
    struct
    {
        bool no_recoil = false;
        bool explosives = true;
        bool death_bags = true;
        bool tracers = false;
        bool radar_directions = true;
        bool radar_team = false;
        bool radar_smoothing = true;
        bool auto_join = false;
        bool anti_afk = false;
        bool feature_hud = false;
        bool build_x = false;
        int tracer_style = 0; // rainbow trail, per-shot rainbow, solid, gradient
        int mortar_mode = 0;  // auto aim, manual impact radar
        float explosive_range = 200.f;
        float bag_range = 150.f;
        float tracer_lifetime = 2.f;
        float tracer_width = 1.5f;
        float radar_arrow_size = 5.f;
        float tracer_color[4] = {0.f, 0.62f, 1.f, 1.f};
        float tracer_end_color[4] = {1.f, 0.15f, 0.65f, 1.f};
        float explosive_color[4] = {1.f, 0.35f, 0.1f, 1.f};
        float bag_color[4] = {1.f, 0.78f, 0.35f, 1.f};
        int player_text = 0; // 0: both above, 1: retired, 2: distance, 3: name, 4: off
    } extra;

    // Version 4: radar content is independent of world ESP.
    struct
    {
        bool enemies = true;
        bool downed = false;
        bool helicopters = true;
        bool ground = true;
        bool boats = true;
        bool stationary = true;
        bool items = false;
        bool explosives = false;
        bool bags = false;
    } radar;

    // Version 5: append to preserve older saved settings.
    float selected_visible_color[4] = {0, 1, 1, 1};

    // Version 6: Magic can steer toward an occluded selected target.
    bool magic_ignore_visibility = true;

    // Version 7: Silent handles nearer targets when both projectile modes are on.
    float magic_min_distance = 0.f;
};

namespace wdgs::actors
{
    enum class Kind : std::uint8_t;
}
bool radar_vehicle_visible(const Config& value, wdgs::actors::Kind kind);
float radar_scan_range(const Config& value);

inline Config config;
bool save_config();
bool load_config();
void validate_config(Config& value);
bool copy_config();
bool paste_config();
std::string encode_config(const Config& value);
bool decode_config(const char* text, std::size_t length, Config& value);
