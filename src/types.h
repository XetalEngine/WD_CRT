#pragma once
#include "stdafx.h"
#include "structs.h"

#define IPC_MAX_PLAYERS 100

enum BoneIdx
{
    BONE_HEAD = 0,
    BONE_NECK,
    BONE_CHEST,
    BONE_PELVIS,
    BONE_UPPER_ARM_L,
    BONE_LOWER_ARM_L,
    BONE_HAND_L,
    BONE_UPPER_ARM_R,
    BONE_LOWER_ARM_R,
    BONE_HAND_R,
    BONE_THIGH_L,
    BONE_CALF_L,
    BONE_FOOT_L,
    BONE_THIGH_R,
    BONE_CALF_R,
    BONE_FOOT_R,
    BONE_CLAVICLE_L,
    BONE_CLAVICLE_R,
    BONE_ROOT,
    BONE_COUNT
};

struct Player
{
    wchar_t player_name[32];
    wchar_t weapon_name[64];
    int32_t weapon_quality;
    FVector bones[BONE_COUNT];
    FVector world_pos;
    FVector velocity;
    bool has_bones;
    float health;
    float max_health;
    float armor;
    float distance;
    bool isVisible;
    bool is_in_team;
    bool is_on_mortar;
    bool is_in_vehicle;
};

struct CameraIPC
{
    FVector location;
    FRotator rotation;
    float fov;
};
