#include "overlay.h"
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <dwrite.h>

namespace
{
    HWND target_window;
    IDirect3D9Ex* api9;
    IDirect3DDevice9Ex* device9;
    IDirect3DSurface9* backbuffer;
    ID3D11Device* device11;
    ID3D11DeviceContext* context11;
    ID2D1Factory1* factory;
    ID2D1Device* device2d;
    ID2D1DeviceContext* context;
    ID2D1SolidColorBrush* brush;
    IDWriteFactory* text_factory;
    IDWriteTextFormat* format;
    D3DPRESENT_PARAMETERS present{};
    bool drawing;

    enum class Stage
    {
        draw,
        copy,
        present
    };
    struct Frame
    {
        IDirect3DTexture9* texture9;
        IDirect3DSurface9* surface9;
        IDirect3DQuery9* finished9;
        ID3D11Texture2D* texture11;
        ID3D11Query* finished11;
        ID2D1Bitmap1* target;
        Stage stage;
    };
    Frame frames[2]{};
    unsigned read_index, write_index, pending;
#ifdef WD_TEST
    unsigned blocked_stage;
    bool blocked_seen;
    unsigned text_layouts;
#endif

    struct TextEntry
    {
        IDWriteTextLayout* layout = nullptr;
        wchar_t value[128]{};
        float size = 0;
        float width = 0;
        float height = 0;
        unsigned hash = 0, used = 0;
    };
    TextEntry text_cache[128][8];
    unsigned text_clock;

    template <typename T>
    void release(T*& p)
    {
        if (p)
            p->Release();
        p = nullptr;
    }

    void release_target()
    {
        if (drawing)
            context->EndDraw();
        drawing = false;
        if (context)
            context->SetTarget(nullptr);
        for (auto& frame : frames)
            release(frame.target);
        if (context11)
        {
            context11->ClearState();
            context11->Flush();
        }
        for (auto& frame : frames)
        {
            release(frame.texture11);
            release(frame.surface9);
            release(frame.texture9);
        }
        read_index = write_index = pending = 0;
    }

    bool create_target(Frame& frame)
    {
        HANDLE shared = nullptr;
        IDXGISurface* surface = nullptr;
        HRESULT hr = device9->CreateTexture(present.BackBufferWidth, present.BackBufferHeight, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &frame.texture9, &shared);
        if (SUCCEEDED(hr))
            hr = frame.texture9->GetSurfaceLevel(0, &frame.surface9);
        if (SUCCEEDED(hr))
            hr = device11->OpenSharedResource(shared, IID_PPV_ARGS(&frame.texture11));
        if (SUCCEEDED(hr))
            hr = frame.texture11->QueryInterface(IID_PPV_ARGS(&surface));
        const D2D1_BITMAP_PROPERTIES1 properties{{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.f, 96.f, D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, nullptr};
        if (SUCCEEDED(hr))
            hr = context->CreateBitmapFromDxgiSurface(surface, &properties, &frame.target);
        release(surface);
        return SUCCEEDED(hr);
    }

    HRESULT advance(Frame& frame)
    {
        if (frame.stage == Stage::draw)
        {
            BOOL ready = FALSE;
            HRESULT hr = context11->GetData(frame.finished11, &ready, sizeof(ready), D3D11_ASYNC_GETDATA_DONOTFLUSH);
#ifdef WD_TEST
            if (blocked_stage == 1)
            {
                blocked_seen = true;
                hr = S_FALSE;
            }
#endif
            if (hr != S_OK)
                return hr;
            if (!ready)
                return S_FALSE;
            hr = device9->StretchRect(frame.surface9, nullptr, backbuffer, nullptr, D3DTEXF_NONE);
            if (SUCCEEDED(hr))
                hr = frame.finished9->Issue(D3DISSUE_END);
            if (FAILED(hr))
                return hr;
            frame.stage = Stage::copy;
        }
        if (frame.stage == Stage::copy)
        {
            HRESULT hr = frame.finished9->GetData(nullptr, 0, D3DGETDATA_FLUSH);
#ifdef WD_TEST
            if (blocked_stage == 2)
            {
                blocked_seen = true;
                hr = S_FALSE;
            }
#endif
            if (hr != S_OK)
                return hr;
            frame.stage = Stage::present;
        }
#ifdef WD_TEST
        if (blocked_stage == 3)
        {
            blocked_seen = true;
            return S_FALSE;
        }
#endif
        const HRESULT hr = device9->PresentEx(nullptr, nullptr, nullptr, nullptr, D3DPRESENT_DONOTWAIT);
        return hr == D3DERR_WASSTILLDRAWING ? S_FALSE : hr;
    }
} // namespace

POINT overlay::origin{};

HWND overlay::window()
{
    return target_window;
}

void overlay::shutdown()
{
    release_target();
    for (auto& bucket : text_cache)
        for (auto& entry : bucket)
            release(entry.layout);
    text_clock = 0;
    release(brush);
    release(context);
    release(device2d);
    release(factory);
    release(format);
    release(text_factory);
    for (auto& frame : frames)
    {
        release(frame.finished11);
        release(frame.finished9);
    }
    release(context11);
    release(device11);
    release(backbuffer);
    release(device9);
    release(api9);
    target_window = nullptr;
}

bool overlay::initialize(HWND window)
{
    shutdown();
    RECT bounds{};
    if (!window || !GetClientRect(window, &bounds) || bounds.right <= 0 || bounds.bottom <= 0)
        return false;
    present = {};
    present.BackBufferWidth = bounds.right;
    present.BackBufferHeight = bounds.bottom;
    present.BackBufferFormat = D3DFMT_A8R8G8B8;
    present.BackBufferCount = 1;
    present.SwapEffect = D3DSWAPEFFECT_COPY;
    present.hDeviceWindow = window;
    present.Windowed = TRUE;
    present.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &api9);
    if (SUCCEEDED(hr))
        hr = api9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &present, nullptr, &device9);
    if (SUCCEEDED(hr))
        hr = device9->SetMaximumFrameLatency(1);
    if (SUCCEEDED(hr))
        hr = device9->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
    LUID luid{};
    IDXGIFactory4* adapters = nullptr;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(hr))
        hr = api9->GetAdapterLUID(D3DADAPTER_DEFAULT, &luid);
    if (SUCCEEDED(hr))
        hr = CreateDXGIFactory1(IID_PPV_ARGS(&adapters));
    if (SUCCEEDED(hr))
        hr = adapters->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
    if (SUCCEEDED(hr))
        hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device11, nullptr, &context11);
    release(adapter);
    release(adapters);
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
    for (auto& frame : frames)
    {
        if (SUCCEEDED(hr))
            hr = device11->CreateQuery(&query, &frame.finished11);
        if (SUCCEEDED(hr))
            hr = device9->CreateQuery(D3DQUERYTYPE_EVENT, &frame.finished9);
    }
    IDXGIDevice* dxgi = nullptr;
    if (SUCCEEDED(hr))
        hr = device11->QueryInterface(IID_PPV_ARGS(&dxgi));
    if (SUCCEEDED(hr))
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateDevice(dxgi, &device2d);
    if (SUCCEEDED(hr))
        hr = device2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context);
    if (SUCCEEDED(hr))
        hr = context->CreateSolidColorBrush(white, &brush);
    if (SUCCEEDED(hr))
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&text_factory));
    if (SUCCEEDED(hr))
        hr = text_factory->CreateTextFormat(L"Tahoma", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.f, L"en-us", &format);
    release(dxgi);
    if (FAILED(hr) || !create_target(frames[0]) || !create_target(frames[1]))
    {
        shutdown();
        return false;
    }
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    origin = {};
    ClientToScreen(window, &origin);
    screen_width = bounds.right;
    screen_height = bounds.bottom;
    target_window = window;
#ifdef WD_TEST
    text_layouts = 0;
#endif
    return true;
}

bool overlay::flush()
{
    if (!target_window || drawing)
        return false;
    // Each surface must finish drawing and copying before it can be reused.
    for (unsigned i = 0; i < 2 && pending; ++i)
    {
        auto& frame = frames[read_index];
        const HRESULT hr = advance(frame);
        if (hr == S_FALSE)
            return false;
        if (FAILED(hr))
        {
            // log(""overlay submission failed (0x%08lX)", static_cast<unsigned long>(hr));
            shutdown();
            return false;
        }
        --pending;
        read_index ^= 1;
    }
    return pending == 0;
}

bool overlay::begin()
{
    if (!target_window || drawing)
        return false;
    flush();
    if (!target_window || pending == 2)
        return false;
    context->SetTarget(frames[write_index].target);
    context->BeginDraw();
    context->Clear({0, 0, 0, 0});
    drawing = true;
    return true;
}

bool overlay::end()
{
    if (!drawing)
        return false;
    HRESULT hr = context->EndDraw();
    drawing = false;
    context->SetTarget(nullptr);
    if (FAILED(hr))
    {
        shutdown();
        return false;
    }
    auto& frame = frames[write_index];
    frame.stage = Stage::draw;
    context11->End(frame.finished11);
    context11->Flush();
    ++pending;
    write_index ^= 1;
    return target_window != nullptr;
}

void overlay::line(float x1, float y1, float x2, float y2, Color color, float thickness)
{
    if (!drawing)
        return;
    brush->SetColor(color);
    context->DrawLine({x1, y1}, {x2, y2}, brush, thickness);
}

void overlay::rect(float x, float y, float w, float h, Color color, bool fill, float thickness)
{
    if (!drawing)
        return;
    brush->SetColor(color);
    const D2D1_RECT_F r{x, y, x + w, y + h};
    if (fill)
        context->FillRectangle(r, brush);
    else
        context->DrawRectangle(r, brush, thickness);
}

void overlay::circle(float x, float y, float radius, Color color, bool fill, float thickness)
{
    if (!drawing || radius <= 0)
        return;
    brush->SetColor(color);
    const D2D1_ELLIPSE shape{{x, y}, radius, radius};
    if (fill)
        context->FillEllipse(shape, brush);
    else
        context->DrawEllipse(shape, brush, thickness);
}

static TextEntry* cached_text(const wchar_t* value, float size)
{
    if (!drawing || !value || !*value)
        return nullptr;
    unsigned hash = 2166136261u;
    unsigned count = 0;
    while (count < 127 && value[count])
        hash = (hash ^ value[count++]) * 16777619u;
    // Keep colliding labels together; evict only the oldest of these eight entries.
    auto& bucket = text_cache[hash & 127];
    auto* selected = &bucket[0];
    const unsigned used = ++text_clock;
    for (auto& cached : bucket)
    {
        if (cached.layout && cached.hash == hash && cached.size == size && wcsncmp(cached.value, value, 127) == 0)
        {
            selected = &cached;
            break;
        }
        if (!cached.layout || (selected->layout && used - cached.used > used - selected->used))
            selected = &cached;
    }
    auto& entry = *selected;
    if (!entry.layout || entry.size != size || wcsncmp(entry.value, value, 127) != 0)
    {
        release(entry.layout);
        if (FAILED(text_factory->CreateTextLayout(value, count, format, 4096.f, 128.f, &entry.layout)))
            return nullptr;
#ifdef WD_TEST
        ++text_layouts;
#endif
        entry.layout->SetFontSize(size, {0, count});
        DWRITE_TEXT_METRICS metrics{};
        entry.layout->GetMetrics(&metrics);
        entry.width = metrics.widthIncludingTrailingWhitespace;
        entry.height = metrics.height;
        entry.size = size;
        entry.hash = hash;
        wcsncpy_s(entry.value, value, count);
    }
    entry.used = used;
    return &entry;
}

void overlay::text(float x, float y, const wchar_t* value, Color color, float size, bool centered, Color background)
{
    const auto* cached = cached_text(value, size);
    if (!cached)
        return;
    const auto& entry = *cached;
    if (centered)
        x -= entry.width * 0.5f;
    if (background.a > 0)
        rect(x - 4, y - 2, entry.width + 8, entry.height + 4, background);
    brush->SetColor(color);
    context->DrawTextLayout({x, y}, entry.layout, brush);
}

void overlay::text_pair(float x, float y, const wchar_t* prefix, const wchar_t* value, Color color, float size, Color background)
{
    const auto* first = cached_text(prefix, size);
    const auto* second = cached_text(value, size);
    if (!first || !second)
        return;
    const float width = first->width + second->width;
    x -= width * 0.5f;
    if (background.a > 0)
        rect(x - 4, y - 2, width + 8, std::max(first->height, second->height) + 4, background);
    brush->SetColor(color);
    context->DrawTextLayout({x, y}, first->layout, brush);
    context->DrawTextLayout({x + first->width, y}, second->layout, brush);
}

void overlay::text(float x, float y, const char* value, Color color, float size, bool centered, Color background)
{
    wchar_t wide[128]{};
    if (!value || !MultiByteToWideChar(CP_UTF8, 0, value, -1, wide, 128))
        return;
    text(x, y, wide, color, size, centered, background);
}

#ifdef WD_TEST
unsigned overlay::test_text_layouts()
{
    return text_layouts;
}

void overlay::test_block_gpu(unsigned stage)
{
    blocked_stage = stage;
    blocked_seen = false;
}

bool overlay::test_gpu_blocked()
{
    return blocked_seen;
}

bool overlay::capture(const wchar_t* path)
{
    const auto start = GetTickCount64();
    while (!flush())
    {
        if (!target_window || drawing || GetTickCount64() - start >= 2000)
            return false;
        Sleep(1);
    }
    IDirect3DSurface9* back = nullptr;
    IDirect3DSurface9* copy = nullptr;
    HRESULT hr = device9->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    if (SUCCEEDED(hr))
        hr = device9->CreateOffscreenPlainSurface(screen_width, screen_height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &copy, nullptr);
    if (SUCCEEDED(hr))
        hr = device9->GetRenderTargetData(back, copy);
    D3DLOCKED_RECT pixels{};
    if (SUCCEEDED(hr))
        hr = copy->LockRect(&pixels, nullptr, D3DLOCK_READONLY);
    FILE* file = nullptr;
    if (SUCCEEDED(hr) && _wfopen_s(&file, path, L"wb") == 0)
    {
        BITMAPFILEHEADER header{0x4D42, static_cast<DWORD>(54 + screen_width * screen_height * 4), 0, 0, 54};
        BITMAPINFOHEADER info{40, screen_width, -screen_height, 1, 32, BI_RGB};
        fwrite(&header, sizeof(header), 1, file);
        fwrite(&info, sizeof(info), 1, file);
        for (int row = 0; row < screen_height; ++row)
            fwrite(static_cast<const char*>(pixels.pBits) + row * pixels.Pitch, screen_width * 4, 1, file);
        fclose(file);
    }
    else
        hr = E_FAIL;
    if (pixels.pBits)
        copy->UnlockRect();
    release(copy);
    release(back);
    return SUCCEEDED(hr);
}
#endif
