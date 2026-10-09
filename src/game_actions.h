#pragma once
#include "config.h"

namespace game_actions
{
    struct BuildStatus
    {
        const char* delivery = "not requested";
        const char* result = "not run";
    };
    BuildStatus build_status();
    void update(const Config& settings, bool menu_visible);
    void stop();
#ifdef WD_TEST
    void test_service(const Config& settings, ULONGLONG now);
#endif
} // namespace game_actions
