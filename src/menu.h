#pragma once
#include "stdafx.h"

namespace menu
{
    void update();
    void draw();
    bool stop_requested();
#ifdef WD_TEST
    void test_input(int tab, float x, float y, bool click, bool down = false);
    void test_mouse(float x, float y, bool down);
    int test_tab();
#endif
} // namespace menu
