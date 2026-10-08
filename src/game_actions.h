#pragma once
#include "config.h"

namespace game_actions
{
    void update(const Config& settings, bool menu_visible);
    void stop();
#ifdef WD_TEST
    void test_service(const Config& settings, ULONGLONG now);
#endif
} // namespace game_actions
