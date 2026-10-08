#pragma once
#include "game.h"
#include "projectile_subsystem.h"

namespace tracers
{
    void reset();
    void tick(game::Snapshot& output, std::uint32_t pawn, std::uint32_t vehicle, bool enabled, float lifetime);
    void sample(const wdgs::projectile_subsystem::Instance& round, std::uintptr_t pool, double now);
    void snapshot(game::Snapshot& output, double now, float lifetime);
} // namespace tracers
