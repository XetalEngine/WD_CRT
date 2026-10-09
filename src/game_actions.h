#pragma once
#include "config.h"

namespace game_actions
{
    struct Status
    {
        const char* delivery = xor_text("not requested");
        const char* result = xor_text("not run");
    };
    Status build_status();
    Status faction_status();
    void update(const Config& settings, bool menu_visible);
    void stop();
#ifdef WD_TEST
    void test_service(const Config& settings, ULONGLONG now);
#endif
} // namespace game_actions
