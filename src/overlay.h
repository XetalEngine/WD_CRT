#pragma once
#include "stdafx.h"
#include <d2d1_1.h>

namespace overlay
{
    using Color = D2D1_COLOR_F;
    constexpr Color white{0.93f, 0.94f, 0.96f, 1.f};
    constexpr Color accent{0.30f, 0.70f, 0.95f, 1.f};
    bool initialize(HWND window);
    bool begin();
    bool end();
    bool flush();
    void shutdown();
    HWND window();
    extern POINT origin;
    void line(float x1, float y1, float x2, float y2, Color color, float thickness = 1.f);
    void rect(float x, float y, float w, float h, Color color, bool fill = true, float thickness = 1.f);
    void circle(float x, float y, float radius, Color color, bool fill = false, float thickness = 1.f);
    void text(float x, float y, const wchar_t* value, Color color = white, float size = 14.f, bool centered = false, Color background = {});
    void text(float x, float y, const char* value, Color color = white, float size = 14.f, bool centered = false, Color background = {});
    inline Color color(const float* c)
    {
        return {c[0], c[1], c[2], c[3]};
    }
#ifdef WD_TEST
    unsigned test_text_layouts();
    void test_block_gpu(unsigned stage);
    bool test_gpu_blocked();
    bool capture(const wchar_t* path);
#endif
} // namespace overlay
