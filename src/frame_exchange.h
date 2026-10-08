#pragma once
#include "config.h"
#include "game.h"
#include <utility>

struct FrameExchange
{
    // The updater, handoff and renderer each own a separate snapshot buffer.
    SRWLOCK mutex = SRWLOCK_INIT;
    game::Snapshot completed;
    Config settings;
    bool menu_visible = true;
    bool changed = false;
    bool running = true;

    bool read_inputs(Config& output, bool& visible)
    {
        AcquireSRWLockExclusive(&mutex);
        const bool active = running;
        output = settings;
        visible = menu_visible;
        ReleaseSRWLockExclusive(&mutex);
        return active;
    }

    bool publish(game::Snapshot& next)
    {
        AcquireSRWLockExclusive(&mutex);
        const bool active = running;
        if (active)
        {
            std::swap(completed, next);
            changed = true;
        }
        ReleaseSRWLockExclusive(&mutex);
        return active;
    }

    bool refresh(game::Snapshot& displayed, const Config& input, bool visible)
    {
        if (!TryAcquireSRWLockExclusive(&mutex))
            return false;
        settings = input;
        menu_visible = visible;
        const bool updated = changed;
        if (updated)
        {
            std::swap(displayed, completed);
            changed = false;
        }
        ReleaseSRWLockExclusive(&mutex);
        return updated;
    }

    void stop()
    {
        AcquireSRWLockExclusive(&mutex);
        running = false;
        ReleaseSRWLockExclusive(&mutex);
    }
};
