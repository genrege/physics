// physics.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "physics.h"
#include "simulation.h"
#include "coordinate_translation.h"
                                                                    
#include <atomic>
#include <chrono>
#include <string>
#include <memory>
#include <d2d1.h>
#pragma comment(lib, "d2d1")
#include <dwrite.h>
#pragma comment(lib, "dwrite")

#include "simulation_xml.h"
#include "physics_engine.h"
#include "sim_runner.h"
#include "editor_ui.h"
#include "info_format.h"
#include "speed_pacer.h"
#include "label_layout.h"
#include <functional>
#include <limits>
#include <optional>
#include <unordered_map>

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

double scale = 1.0;

// Pan state for click-drag panning
double pan_x = 0.0;
double pan_y = 0.0;
bool panning = false;
POINT pan_start_pt = {0,0};
double pan_start_x = 0.0;
double pan_start_y = 0.0;

std::atomic<double> simulation_clock{ 0.0 };   // simulated seconds; written by the runner thread, read by the UI

// Speed is in multiples of real time (simulated seconds per wall-clock second).
std::atomic<double> speed_multiplier{ 1.0 };
std::atomic<bool> paused{ false };
std::atomic<bool> pacing_reset{ true };        // the runner restarts its pacing from now
std::atomic<bool> falling_behind{ false };     // the last frame could not reach the requested speed
double speed_base = 1.0;                       // the loaded simulation's own starting speed
double achieved_speed = 0.0;                   // measured with the FPS, UI thread only
double fps_window_clock = 0.0;
HMENU g_speed_menu = NULL;
constexpr double speed_presets[] = { 0.01, 0.1, 1.0, 10.0, 100.0, 1000.0 };

// UI thread only.
bool labels_visible = true;
std::optional<size_t> tracked;   // the mass kept in the middle of the window
HMENU g_view_menu = NULL;
std::unordered_map<std::string, label_size> label_size_cache;

std::wstring widen(const std::string& text)
{
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(size_t(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), &result[0], length);
    return result;
}

// Displayed frames per second, measured over half-second windows on the UI thread.
double displayed_fps = 0.0;
unsigned fps_frames = 0;
std::chrono::steady_clock::time_point fps_last_frame = std::chrono::steady_clock::now();
std::chrono::steady_clock::time_point fps_window_start = std::chrono::steady_clock::now();
HWND hwnd_client = 0;
CRITICAL_SECTION cs;

std::vector<mass_state> mass_states;
std::vector<spring_state> spring_states;
std::vector<damper_state> damper_states;

std::shared_ptr<simulation> sim;

// True while the editor owns the window; the simulation thread is stopped.
bool editing = false;

struct world_bounds { double xmin, xmax, ymin, ymax; };

world_bounds current_bounds()
{
    if (editing)
    {
        const auto& w = editor_ui::model().document().world;
        return { w.xmin, w.xmax, w.ymin, w.ymax };
    }
    return { sim->xmin(), sim->xmax(), sim->ymin(), sim->ymax() };
}

// Same mapping as the run-mode viewport so the editor shows what will run.
editor_view make_editor_view()
{
    RECT rc;
    GetClientRect(hwnd_client, &rc);
    const double width = double((std::max)(rc.right - rc.left, LONG(1)));
    const double height = double((std::max)(rc.bottom - rc.top, LONG(1)));
    const double aspect_ratio = width / height;
    const auto b = current_bounds();

    editor_view view;
    view.canvas = rc;
    view.xmin = scale * b.xmin * aspect_ratio + pan_x;
    view.xmax = scale * b.xmax * aspect_ratio + pan_x;
    view.ymin = scale * b.ymin + pan_y;
    view.ymax = scale * b.ymax + pan_y;
    return view;
}

// The engine is only touched by the runner thread while it runs, and by the UI thread while it is stopped.
std::unique_ptr<physics_engine> engine;
std::unique_ptr<sim_runner> runner;
engine_kind current_engine_kind = engine_kind::cpu;
HMENU g_engine_menu = NULL;
std::atomic<bool> engine_failed{ false };
std::string engine_failure_message;   // guarded by cs

// Direct2D globals
ID2D1Factory*        g_pD2DFactory = nullptr;
ID2D1HwndRenderTarget* g_pRenderTarget = nullptr;
ID2D1SolidColorBrush*  g_pBrushGreen = nullptr;
ID2D1SolidColorBrush*  g_pBrushRed = nullptr;
ID2D1SolidColorBrush*  g_pBrushBlue = nullptr;
ID2D1SolidColorBrush*  g_pBrushCustom = nullptr;   // recoloured per object for custom colours
IDWriteFactory*        g_pDWriteFactory = nullptr;
IDWriteTextFormat*     g_pTextFormat = nullptr;
IDWriteTextFormat*     g_pLabelFormat = nullptr;

// Persistent GDI objects (fallback)
HPEN g_hGreenPen = NULL;
HBRUSH g_hRedBrush = NULL;
HBRUSH g_hBlueBrush = NULL;

// Persistent backbuffer (fallback)
HDC   g_hMemDC = NULL;
HBITMAP g_hBackBitmap = NULL;
HBITMAP g_hOldBackBitmap = NULL;
int   g_backbuf_w = 0;
int   g_backbuf_h = 0;

// SafeRelease helper
template<typename T>
inline void SafeRelease(T** ppT)
{
    if (ppT && *ppT)
    {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

// Create and recreate Direct2D device resources
HRESULT CreateDeviceResources(HWND hWnd)
{
    if (!g_pD2DFactory)
    {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory);
        if (FAILED(hr)) return hr;
    }

    if (g_pRenderTarget)
        return S_OK;

    RECT rc;
    GetClientRect(hWnd ? hWnd : hwnd_client, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    HRESULT hr = g_pD2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hWnd ? hWnd : hwnd_client, size),
        &g_pRenderTarget);

    if (SUCCEEDED(hr))
    {
        hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Lime), &g_pBrushGreen);
        if (SUCCEEDED(hr))
            hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), &g_pBrushRed);
        if (SUCCEEDED(hr))
            hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Blue), &g_pBrushBlue);
        if (SUCCEEDED(hr))
            hr = g_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &g_pBrushCustom);
    }

    return hr;
}

void DiscardDeviceResources()
{
    SafeRelease(&g_pBrushGreen);
    SafeRelease(&g_pBrushRed);
    SafeRelease(&g_pBrushBlue);
    SafeRelease(&g_pBrushCustom);
    SafeRelease(&g_pRenderTarget);
}

// Runs on the sim_runner thread; it never blocks on the UI.
bool simulation_step()
{
    static speed_pacer pacer;                                   // runner thread only
    static std::chrono::steady_clock::time_point last_call;
    static size_t chunk_steps = 16;

    const auto frame_start = std::chrono::steady_clock::now();
    if (pacing_reset.exchange(false))
    {
        pacer.reset();
        last_call = frame_start;
        chunk_steps = 16;
    }
    // A long gap (a pause, an engine switch) must not turn into a burst of steps.
    const double wall_seconds = (std::min)(std::chrono::duration<double>(frame_start - last_call).count(), 0.1);
    last_call = frame_start;

    if (engine_failed)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return false;
    }
    if (paused)
    {
        falling_behind = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        return false;
    }

    const double dt = sim->dt();
    const size_t wanted = pacer.steps_for(speed_multiplier.load(), dt, wall_seconds);
    if (wanted == 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return false;   // nothing new to show; repainting would count a duplicate frame
    }

    // Run in chunks, spending at most ~40 ms per frame, so a speed the machine cannot reach stays responsive;
    // the shortfall is dropped rather than queued.
    constexpr double frame_budget = 0.04;
    size_t done = 0;

    try
    {
        while (done < wanted)
        {
            const size_t n = (std::min)(wanted - done, chunk_steps);
            const auto chunk_start = std::chrono::steady_clock::now();
            engine->step(n, dt);
            const auto chunk_end = std::chrono::steady_clock::now();
            done += n;

            const double took = std::chrono::duration<double>(chunk_end - chunk_start).count();
            if (took > 0.0)
                chunk_steps = (std::min)((std::max)(size_t(double(n) * 0.005 / took), size_t(1)), size_t(1000000));   // aim for ~5 ms chunks
            if (std::chrono::duration<double>(chunk_end - frame_start).count() > frame_budget)
                break;
        }

        EnterCriticalSection(&cs);
        engine->download_render(mass_states, spring_states);
        LeaveCriticalSection(&cs);
    }
    catch (const std::exception& ex)
    {
        EnterCriticalSection(&cs);
        engine_failure_message = ex.what();
        LeaveCriticalSection(&cs);
        if (!engine_failed.exchange(true))
            PostMessage(hwnd_client, WM_USER + 2, 0, 0);
        return false;
    }

    falling_behind = done < wanted;
    simulation_clock.store(simulation_clock.load() + dt * double(done));
    return true;
}

// Always restart through here so pacing begins from the current moment.
void start_runner()
{
    pacing_reset = true;
    runner->start();
}

// The control range is 0.01x to 1000x, widened for simulations whose own speed lies outside it.
double speed_min() { return (std::min)(0.01, speed_base / 1000.0); }
double speed_max() { return (std::max)(1000.0, speed_base * 1000.0); }

void adopt_simulation()
{
    tracked.reset();
    speed_base = sim->speed();
    speed_multiplier = speed_base;
    paused = false;
    falling_behind = false;
}

void set_speed(double multiple)
{
    speed_multiplier = (std::min)((std::max)(multiple, speed_min()), speed_max());
    InvalidateRect(hwnd_client, NULL, FALSE);   // the label changes even while paused
}

// Check marks follow the current speed; the menu is disabled while editing.
void update_speed_menu()
{
    if (!g_speed_menu)
        return;

    const double current = speed_multiplier.load();
    const UINT enabled = editing ? MF_GRAYED : MF_ENABLED;
    for (int i = 0; i < int(std::size(speed_presets)); ++i)
    {
        const bool match = std::abs(current - speed_presets[i]) <= 1e-9 * speed_presets[i];
        CheckMenuItem(g_speed_menu, ID_SPEED_PRESET_FIRST + i, MF_BYCOMMAND | (match ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(g_speed_menu, ID_SPEED_PRESET_FIRST + i, MF_BYCOMMAND | enabled);
    }
    for (UINT id : { ID_SPEED_FASTER, ID_SPEED_SLOWER, ID_SPEED_RESET, ID_SPEED_PAUSE })
        EnableMenuItem(g_speed_menu, id, MF_BYCOMMAND | enabled);
    CheckMenuItem(g_speed_menu, ID_SPEED_PAUSE, MF_BYCOMMAND | (paused ? MF_CHECKED : MF_UNCHECKED));
}

void update_view_menu()
{
    if (!g_view_menu)
        return;
    CheckMenuItem(g_view_menu, ID_VIEW_LABELS, MF_BYCOMMAND | (labels_visible ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(g_view_menu, ID_VIEW_STOP_TRACKING, MF_BYCOMMAND | (tracked && !editing ? MF_ENABLED : MF_GRAYED));
}

void update_engine_ui()
{
    if (g_engine_menu)
        CheckMenuRadioItem(g_engine_menu, ID_ENGINE_CPU, ID_ENGINE_CUDA, current_engine_kind == engine_kind::cuda ? ID_ENGINE_CUDA : ID_ENGINE_CPU, MF_BYCOMMAND);

    std::wstring title = szTitle;
    const char* engine_name = engine->name();
    title += L" - ";
    title.append(engine_name, engine_name + strlen(engine_name));
    if (editing)
        title += L" - editing " + editor_ui::document_title();
    SetWindowTextW(hwnd_client, title.c_str());
}

// UI thread only, with the runner stopped. Moves the simulation into an engine of the requested kind,
// falling back to the CPU engine if CUDA cannot be created or loaded.
void select_engine(engine_kind kind, bool carry_state)
{
    if (engine && carry_state)
    {
        try
        {
            engine->download(mass_states, spring_states, damper_states);
        }
        catch (const std::exception&)
        {
            // keep the last rendered snapshot
        }
    }

    std::string error;
    auto next = make_engine(kind, error);
    auto actual = (kind == engine_kind::cuda && error.empty()) ? engine_kind::cuda : engine_kind::cpu;

    try
    {
        next->upload(*sim, mass_states, spring_states, damper_states);
    }
    catch (const std::exception& ex)
    {
        error = ex.what();
        next = make_cpu_engine();
        next->upload(*sim, mass_states, spring_states, damper_states);
        actual = engine_kind::cpu;
    }

    if (!error.empty())
        MessageBoxA(hwnd_client, error.c_str(), "CUDA engine unavailable, using CPU", MB_OK | MB_ICONWARNING);

    engine = std::move(next);
    current_engine_kind = actual;
    engine_failed = false;
    update_engine_ui();
}

std::wstring make_info_text()
{
    const wchar_t* engine_name = current_engine_kind == engine_kind::cuda ? L"CUDA" : L"CPU";
    if (editing)
        return std::wstring(L"Editor  |  ") + engine_name;

    wchar_t fps[32];
    const double shown_fps = paused ? 0.0 : displayed_fps;
    swprintf_s(fps, shown_fps < 10.0 ? L"%.1f FPS" : L"%.0f FPS", shown_fps);

    std::wstring speed = L"Speed " + format_speed(speed_multiplier.load());
    if (paused)
        speed += L" (paused)";
    else if (falling_behind)
        speed += L" (reaching " + format_speed(achieved_speed) + L")";

    std::wstring tracking;
    if (tracked && *tracked < sim->model().masses().size())
    {
        const auto name = widen(label_text(sim->model().masses()[*tracked]));
        tracking = L"  |  Tracking " + (name.empty() ? L"mass " + std::to_wstring(*tracked) : name);
    }

    return std::wstring(engine_name) + L"  |  " + fps + L"  |  Simulation time " + format_duration(simulation_clock.load()) + L"  |  " + speed + tracking;
}

void update_fps()
{
    const auto now = std::chrono::steady_clock::now();
    // Frames slower than the window length are reported individually, otherwise a slow simulation would never update the display.
    const double gap = std::chrono::duration<double>(now - fps_last_frame).count();
    if (gap > 0.5)
    {
        const double clock_now = simulation_clock.load();
        displayed_fps = 1.0 / gap;
        achieved_speed = (clock_now - fps_window_clock) / gap;
        fps_frames = 0;
        fps_window_clock = clock_now;
        fps_window_start = now;
        fps_last_frame = now;
        return;
    }
    fps_last_frame = now;
    ++fps_frames;
    const double elapsed = std::chrono::duration<double>(now - fps_window_start).count();
    if (elapsed >= 0.5)
    {
        displayed_fps = double(fps_frames) / elapsed;
        const double clock_now = simulation_clock.load();
        achieved_speed = (clock_now - fps_window_clock) / elapsed;
        fps_window_clock = clock_now;
        fps_frames = 0;
        fps_window_start = now;
    }
}

// Segoe UI is the font the editor and chat panels use on Windows.
bool ensure_text_format()
{
    if (g_pTextFormat)
        return true;
    if (!g_pDWriteFactory && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_pDWriteFactory))))
        return false;
    return SUCCEEDED(g_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-us", &g_pTextFormat));
}

// Top-left label on a translucent backing so it stays readable over the simulation.
void draw_info_label()
{
    if (!g_pRenderTarget || !g_pBrushCustom || !ensure_text_format())
        return;

    const std::wstring text = make_info_text();
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(g_pDWriteFactory->CreateTextLayout(text.c_str(), UINT32(text.size()), g_pTextFormat, 4000.0f, 40.0f, &layout)))
        return;

    DWRITE_TEXT_METRICS metrics = {};
    layout->GetMetrics(&metrics);
    const float margin = 8.0f;
    const float pad = 7.0f;

    g_pBrushCustom->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.6f));
    g_pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(margin, margin, margin + metrics.width + 2 * pad, margin + metrics.height + 2 * pad), 4.0f, 4.0f), g_pBrushCustom);
    g_pBrushCustom->SetColor(D2D1::ColorF(0.93f, 0.93f, 0.93f));
    g_pRenderTarget->DrawTextLayout(D2D1::Point2F(margin + pad, margin + pad), layout, g_pBrushCustom);
    layout->Release();
}

// Same font as the info label, smaller.
bool ensure_label_format()
{
    if (g_pLabelFormat)
        return true;
    if (!ensure_text_format())
        return false;
    if (FAILED(g_pDWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &g_pLabelFormat)))
        return false;
    g_pLabelFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
}

label_size measure_label_d2d(const std::string& text)
{
    const auto cached = label_size_cache.find(text);
    if (cached != label_size_cache.end())
        return cached->second;
    if (label_size_cache.size() > 4000)
        label_size_cache.clear();

    label_size size;
    const auto wide = widen(text);
    IDWriteTextLayout* layout = nullptr;
    if (!wide.empty() && SUCCEEDED(g_pDWriteFactory->CreateTextLayout(wide.c_str(), UINT32(wide.size()), g_pLabelFormat, 1000.0f, 100.0f, &layout)))
    {
        DWRITE_TEXT_METRICS metrics = {};
        layout->GetMetrics(&metrics);
        size = { metrics.widthIncludingTrailingWhitespace, metrics.height };
        layout->Release();
    }
    label_size_cache.emplace(text, size);
    return size;
}

// Draws the labels that fit: Direct2D, or GDI onto `gdi` when that is the active path.
void draw_labels(HDC gdi, const viewport& vp, const RECT& rc, const simulation& run_sim, const std::vector<mass_state>& states)
{
    if (!labels_visible)
        return;

    const float width = float(rc.right - rc.left);
    const float height = float(rc.bottom - rc.top);

    std::vector<label_body> bodies;
    auto add = [&](size_t i, const vector2& pos, double radius, double m, bool wanted) {
        if (!wanted)
            return;
        const float cx = static_cast<float>(vp.x_to_screen(pos.x()));
        const float cy = static_cast<float>(vp.y_to_screen(pos.y()));
        const float r = fabsf(static_cast<float>(vp.x_to_screen(pos.x() + radius) - vp.x_to_screen(pos.x())));
        if (cx + r < 0 || cy + r < 0 || cx - r > width || cy - r > height)
            return;
        const bool is_tracked = !editing && tracked && *tracked == i;
        bodies.push_back({ i, cx, cy, r, is_tracked ? (std::numeric_limits<double>::max)() : m, wanted });
    };

    std::function<std::string(size_t)> text_of;
    if (editing)
    {
        const auto* masses = &editor_ui::model().document().masses;
        for (size_t i = 0; i < masses->size(); ++i)
            add(i, (*masses)[i].position, (*masses)[i].r, (*masses)[i].m, !((*masses)[i].label && (*masses)[i].label->empty()));
        text_of = [masses](size_t i) { return (*masses)[i].label ? *(*masses)[i].label : format_mass((*masses)[i].m); };
    }
    else
    {
        const auto* masses = &run_sim.model().masses();
        for (size_t i = 0; i < masses->size(); ++i)
            add(i, states[i].position_, (*masses)[i].r(), (*masses)[i].m(), !((*masses)[i].label() && (*masses)[i].label()->empty()));
        text_of = [masses](size_t i) { return label_text((*masses)[i]); };
    }
    if (bodies.empty())
        return;

    HFONT font = NULL;
    HFONT old_font = NULL;
    if (gdi)
    {
        font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        old_font = (HFONT)SelectObject(gdi, font);
        SetBkMode(gdi, TRANSPARENT);
        SetTextColor(gdi, RGB(210, 220, 240));
    }
    else if (!g_pRenderTarget || !g_pBrushCustom || !ensure_label_format())
        return;

    auto measure = [&](size_t id) -> label_size {
        const auto text = text_of(id);
        if (!gdi)
            return measure_label_d2d(text);
        const auto wide = widen(text);
        SIZE extent = {};
        GetTextExtentPoint32W(gdi, wide.c_str(), int(wide.size()), &extent);
        return { float(extent.cx), float(extent.cy) };
    };

    const auto placed = place_labels(std::move(bodies), width, height, measure);

    if (!gdi)
        g_pBrushCustom->SetColor(D2D1::ColorF(0.82f, 0.86f, 0.95f));
    for (const auto& p : placed)
    {
        const auto text = text_of(p.id);
        const auto wide = widen(text);
        if (gdi)
        {
            TextOutW(gdi, int(p.left), int(p.top), wide.c_str(), int(wide.size()));
        }
        else
        {
            const auto size = measure_label_d2d(text);
            g_pRenderTarget->DrawText(wide.c_str(), UINT32(wide.size()), g_pLabelFormat, D2D1::RectF(p.left, p.top, p.left + size.width + 2.0f, p.top + size.height), g_pBrushCustom);
        }
    }

    if (gdi)
    {
        SelectObject(gdi, old_font);
        DeleteObject(font);
    }
}

// The mass drawn under a client point (or just beside it), preferring the nearest and then the heaviest.
std::optional<size_t> mass_at(int px, int py)
{
    RECT rc;
    GetClientRect(hwnd_client, &rc);
    if (rc.right <= rc.left || rc.bottom <= rc.top)
        return std::nullopt;

    const double aspect_ratio = double(rc.right - rc.left) / double(rc.bottom - rc.top);
    const auto bounds = current_bounds();
    viewport vp(rc, scale * bounds.xmin * aspect_ratio + pan_x, scale * bounds.xmax * aspect_ratio + pan_x, scale * bounds.ymin + pan_y, scale * bounds.ymax + pan_y);

    std::optional<size_t> best;
    double best_score = 0.0;
    double best_mass = 0.0;
    const auto& masses = sim->model().masses();

    EnterCriticalSection(&cs);
    for (size_t i = 0; i < masses.size() && i < mass_states.size(); ++i)
    {
        const auto& pos = mass_states[i].position_;
        const double cx = vp.x_to_screen(pos.x());
        const double cy = vp.y_to_screen(pos.y());
        const double r = (std::max)(std::abs(double(vp.x_to_screen(pos.x() + masses[i].r())) - cx), 1.5);
        const double score = std::hypot(cx - px, cy - py) - r;
        if (score > 6.0)
            continue;
        if (!best || score < best_score || (score == best_score && masses[i].m() > best_mass))
        {
            best = i;
            best_score = score;
            best_mass = masses[i].m();
        }
    }
    LeaveCriticalSection(&cs);
    return best;
}

void paint_scene(const HDC hdc, RECT& rc)
{
    const auto aspect_ratio = double(rc.right - rc.left) / double(rc.bottom - rc.top);
    const auto bounds = current_bounds();

    // copy shared state under lock
    EnterCriticalSection(&cs);
    const auto& sim_copy = *sim;   // model is immutable while the runner is active
    const auto mass_states_copy = mass_states;
    const auto spring_states_copy = spring_states;
    LeaveCriticalSection(&cs);

    // The tracked mass stays in the middle of the window.
    if (!editing && tracked && *tracked < mass_states_copy.size())
    {
        const auto& p = mass_states_copy[*tracked].position_;
        pan_x = p.x() - scale * aspect_ratio * (bounds.xmin + bounds.xmax) / 2.0;
        pan_y = p.y() - scale * (bounds.ymin + bounds.ymax) / 2.0;
    }

    // apply pan offsets to world bounds
    const double xmin_world = scale * bounds.xmin * aspect_ratio + pan_x;
    const double xmax_world = scale * bounds.xmax * aspect_ratio + pan_x;
    const double ymin_world = scale * bounds.ymin + pan_y;
    const double ymax_world = scale * bounds.ymax + pan_y;

    viewport vp(rc, xmin_world, xmax_world, ymin_world, ymax_world);

    // Ensure D2D resources exist (try to create if not)
    if (!g_pRenderTarget)
    {
        CreateDeviceResources(hwnd_client);
    }

    if (g_pRenderTarget)
    {
        // Use Direct2D
        g_pRenderTarget->BeginDraw();
        g_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));

        if (editing)
            editor_ui::paint(g_pRenderTarget);
        else
        {
        // Draw springs
        for (size_t i = 0; i < sim_copy.model().springs().size(); ++i)
        {
            if (spring_states_copy[i].broken_)
                continue;
            const auto& spring = sim_copy.model().springs()[i];
            const auto& p1 = mass_states_copy[spring.id_mass1()].position_;
            const auto& p2 = mass_states_copy[spring.id_mass2()].position_;

            const float x1 = static_cast<float>(vp.x_to_screen(p1.x()));
            const float y1 = static_cast<float>(vp.y_to_screen(p1.y()));
            const float x2 = static_cast<float>(vp.x_to_screen(p2.x()));
            const float y2 = static_cast<float>(vp.y_to_screen(p2.y()));

            ID2D1Brush* line_brush = g_pBrushGreen;
            if (spring.colour() && g_pBrushCustom)
            {
                g_pBrushCustom->SetColor(D2D1::ColorF(*spring.colour()));
                line_brush = g_pBrushCustom;
            }
            g_pRenderTarget->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), line_brush, 1.0f);
        }

        // Draw masses (fill then green outline)
        for (size_t i = 0; i < sim_copy.model().masses().size(); ++i)
        {
            const auto& m = sim_copy.model().masses()[i];
            const auto& pos = mass_states_copy[i].position_;

            const float cx = static_cast<float>(vp.x_to_screen(pos.x()));
            const float cy = static_cast<float>(vp.y_to_screen(pos.y()));

            // compute radius in screen pixels using viewport mapping
            const float r_screen = fabsf(static_cast<float>(vp.x_to_screen(pos.x() + m.r()) - vp.x_to_screen(pos.x())));

            // Off-screen masses cost draw calls for nothing.
            if (cx + r_screen < 0 || cy + r_screen < 0 || cx - r_screen > float(rc.right - rc.left) || cy - r_screen > float(rc.bottom - rc.top))
                continue;

            // A custom colour replaces the red/blue fill and the green outline.
            ID2D1Brush* outline_brush = g_pBrushGreen;
            ID2D1Brush* fill_brush = mass_states_copy[i].fixed_ ? g_pBrushBlue : g_pBrushRed;
            if (m.colour() && g_pBrushCustom)
            {
                g_pBrushCustom->SetColor(D2D1::ColorF(*m.colour()));
                outline_brush = g_pBrushCustom;
                fill_brush = g_pBrushCustom;
            }

            // At this size the outline covers the fill, so a small rectangle looks the same and is much cheaper.
            if (r_screen < 1.5f)
            {
                g_pRenderTarget->FillRectangle(D2D1::RectF(cx - 1.0f, cy - 1.0f, cx + 1.0f, cy + 1.0f), outline_brush);
                continue;
            }

            D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), r_screen, r_screen);

            if (fill_brush)
                g_pRenderTarget->FillEllipse(ellipse, fill_brush);

            if (outline_brush)
                g_pRenderTarget->DrawEllipse(ellipse, outline_brush, 1.0f);
        }
        }

        draw_labels(nullptr, vp, rc, sim_copy, mass_states_copy);
        draw_info_label();

        HRESULT hr = g_pRenderTarget->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET)
        {
            DiscardDeviceResources();
        }
    }
    else
    {
        // Fallback to GDI rendering
        HDC hmemDC = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
        HBITMAP oldBmp = (HBITMAP)SelectObject(hmemDC, bmp);

        // clear to black
        RECT fullRc = { 0, 0, rc.right - rc.left, rc.bottom - rc.top };
        FillRect(hmemDC, &fullRc, (HBRUSH)GetStockObject(BLACK_BRUSH));

        // Use global green pen if available, otherwise create a temporary one
        HPEN hPenToUse = g_hGreenPen;
        HPEN hLocalPen = NULL;
        if (!hPenToUse)
        {
            hLocalPen = CreatePen(PS_SOLID, 1, RGB(0,255,0));
            hPenToUse = hLocalPen;
        }
        HPEN hOldPen = (HPEN)SelectObject(hmemDC, hPenToUse);

        HBRUSH hRed = CreateSolidBrush(RGB(255,0,0));
        HBRUSH hBlue = CreateSolidBrush(RGB(0,0,255));

        // Draw springs
        for (size_t i = 0; i < sim_copy.model().springs().size(); ++i)
        {
            if (spring_states_copy[i].broken_)
                continue;
            const auto& spring = sim_copy.model().springs()[i];
            const auto& p1 = mass_states_copy[spring.id_mass1()].position_;
            const auto& p2 = mass_states_copy[spring.id_mass2()].position_;

            const int x1 = vp.x_to_screen(p1.x());
            const int y1 = vp.y_to_screen(p1.y());
            const int x2 = vp.x_to_screen(p2.x());
            const int y2 = vp.y_to_screen(p2.y());

            HPEN hSpringPen = NULL;
            HPEN hPrevPen = NULL;
            if (spring.colour())
            {
                const auto c = *spring.colour();
                hSpringPen = CreatePen(PS_SOLID, 1, RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
                hPrevPen = (HPEN)SelectObject(hmemDC, hSpringPen);
            }

            MoveToEx(hmemDC, x1, y1, NULL);
            LineTo(hmemDC, x2, y2);

            if (hSpringPen)
            {
                SelectObject(hmemDC, hPrevPen);
                DeleteObject(hSpringPen);
            }
        }

        // Draw masses: select fill brush then draw ellipse with green pen outline
        for (size_t i = 0; i < sim_copy.model().masses().size(); ++i)
        {
            const auto& m = sim_copy.model().masses()[i];
            const auto& pos = mass_states_copy[i].position_;

            HBRUSH hFill = mass_states_copy[i].fixed_ ? hBlue : hRed;
            HBRUSH hCustom = NULL;
            if (m.colour())
            {
                const auto c = *m.colour();
                hCustom = CreateSolidBrush(RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
                hFill = hCustom;
            }
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hmemDC, hFill);

            const int left = vp.x_to_screen(pos.x() - m.r()) - 1;
            const int top = vp.y_to_screen(pos.y() - m.r()) - 1;
            const int right = vp.x_to_screen(pos.x() + m.r()) + 1;
            const int bottom = vp.y_to_screen(pos.y() + m.r()) + 1;

            Ellipse(hmemDC, left, top, right, bottom);

            // restore brush
            SelectObject(hmemDC, hOldBrush);
            if (hCustom)
                DeleteObject(hCustom);
        }

        // restore original pen
        SelectObject(hmemDC, hOldPen);
        if (hLocalPen)
            DeleteObject(hLocalPen);

        // cleanup
        DeleteObject(hRed);
        DeleteObject(hBlue);

        // Info label (the GDI path has no DirectWrite)
        draw_labels(hmemDC, vp, rc, sim_copy, mass_states_copy);
        {
            HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT oldFont = (HFONT)SelectObject(hmemDC, font);
            SetBkMode(hmemDC, TRANSPARENT);
            SetTextColor(hmemDC, RGB(235, 235, 235));
            const std::wstring info = make_info_text();
            TextOutW(hmemDC, 14, 12, info.c_str(), int(info.size()));
            SelectObject(hmemDC, oldFont);
            DeleteObject(font);
        }

        // Blit to screen
        BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, hmemDC, 0, 0, SRCCOPY);

        SelectObject(hmemDC, oldBmp);
        DeleteObject(bmp);
        DeleteDC(hmemDC);
    }
}
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_PHYSICS, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_PHYSICS));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PHYSICS));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_PHYSICS);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

// Simulation definitions are deployed to a models folder next to the executable.
std::shared_ptr<simulation> load_simulation(const std::wstring& file_name, std::vector<mass_state>& ms, std::vector<spring_state>& ss, std::vector<damper_state>& ds)
{
    const std::wstring path = editor_ui::models_directory() + L"\\" + file_name;

    std::string error;
    auto loaded = load_simulation_xml(path, ms, ss, ds, error);
    if (!loaded)
        MessageBoxA(hwnd_client, error.c_str(), "Cannot load simulation", MB_OK | MB_ICONERROR);
    return loaded;
}

std::shared_ptr<simulation> load_simulation(int number, std::vector<mass_state>& ms, std::vector<spring_state>& ss, std::vector<damper_state>& ds)
{
    return load_simulation(L"simulation" + std::to_wstring(number) + L".xml", ms, ss, ds);
}

void enter_edit_mode()
{
    if (editing)
        return;
    runner->stop();
    editing = true;
    editor_ui::set_active(true);
    update_engine_ui();
    InvalidateRect(hwnd_client, NULL, FALSE);
    SetFocus(hwnd_client);
}

// Runs the editor's document; the document itself is untouched so F5 returns to it.
void run_document()
{
    std::shared_ptr<simulation> built;
    std::vector<mass_state> ms;
    std::vector<spring_state> ss;
    std::vector<damper_state> ds;
    if (!editor_ui::build_run(built, ms, ss, ds))
        return;

    runner->stop();
    mass_states = std::move(ms);
    spring_states = std::move(ss);
    damper_states = std::move(ds);
    sim = built;
    simulation_clock = 0.0;
    adopt_simulation();
    editing = false;
    editor_ui::set_active(false);
    select_engine(current_engine_kind, false);
    start_runner();
}

void toggle_edit_mode()
{
    if (editing)
        run_document();
    else
        enter_edit_mode();
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   hwnd_client = hWnd;

   InitializeCriticalSection(&cs);

   // Initialize Direct2D device resources
   CreateDeviceResources(hWnd);

   // Create persistent GDI objects (kept for compatibility fallback)
   g_hGreenPen = CreatePen(PS_SOLID, 1, RGB(0, 255, 0));
   g_hRedBrush = CreateSolidBrush(RGB(255, 0, 0));
   g_hBlueBrush = CreateSolidBrush(RGB(0, 0, 255));

   if (!hWnd)
   {
      return FALSE;
   }

    HMENU menu_bar = GetMenu(hWnd);

    // Engine menu is added at runtime so the .rc resource does not need to change.
   g_engine_menu = CreatePopupMenu();
   AppendMenuW(g_engine_menu, MF_STRING, ID_ENGINE_CPU, L"&CPU");
   AppendMenuW(g_engine_menu, MF_STRING | (cuda_device_names().empty() ? MF_GRAYED : 0), ID_ENGINE_CUDA, L"C&UDA");

   // The solar system model is added to the Simulation menu here so physics.rc does not change.
    if (HMENU simulation_menu = GetSubMenu(menu_bar, 1))
   {
      AppendMenuW(simulation_menu, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(simulation_menu, MF_STRING, ID_SIMULATION_SOLAR, L"Solar system (10 Oct 2026)");
      AppendMenuW(simulation_menu, MF_STRING, ID_SIMULATION_MILKYWAY, L"Milky way (50000 stars)");
   }

   g_speed_menu = CreatePopupMenu();
   for (int i = 0; i < int(std::size(speed_presets)); ++i)
      AppendMenuW(g_speed_menu, MF_STRING, ID_SPEED_PRESET_FIRST + i, (format_speed(speed_presets[i]) + L" real time").c_str());
   AppendMenuW(g_speed_menu, MF_SEPARATOR, 0, nullptr);
   AppendMenuW(g_speed_menu, MF_STRING, ID_SPEED_FASTER, L"&Faster\t]");
   AppendMenuW(g_speed_menu, MF_STRING, ID_SPEED_SLOWER, L"&Slower\t[");
   AppendMenuW(g_speed_menu, MF_STRING, ID_SPEED_RESET, L"This simulation's own speed\t\\");
   AppendMenuW(g_speed_menu, MF_STRING, ID_SPEED_PAUSE, L"&Pause / resume\tSpace");

   g_view_menu = CreatePopupMenu();
   AppendMenuW(g_view_menu, MF_STRING | MF_CHECKED, ID_VIEW_LABELS, L"&Labels\tL");
   AppendMenuW(g_view_menu, MF_STRING, ID_VIEW_STOP_TRACKING, L"&Stop tracking\tEsc");

    InsertMenuW(menu_bar, 2, MF_BYPOSITION | MF_POPUP, (UINT_PTR)g_view_menu, L"&View");
    InsertMenuW(menu_bar, 3, MF_BYPOSITION | MF_POPUP, (UINT_PTR)g_speed_menu, L"Sp&eed");
    InsertMenuW(menu_bar, 4, MF_BYPOSITION | MF_POPUP, (UINT_PTR)g_engine_menu, L"&Engine");
   DrawMenuBar(hWnd);

   // InvalidateRect rather than PostMessage: posted messages outrank input, so a fast simulation would starve the menus.
   sim = load_simulation(1, mass_states, spring_states, damper_states);
   if (!sim)
   {
      return FALSE;
   }
   adopt_simulation();

   runner = std::make_unique<sim_runner>(simulation_step, [] { return InvalidateRect(hwnd_client, NULL, FALSE) != 0; });
   select_engine(engine_kind::cpu, false);
   start_runner();

   editor_host host;
   host.invalidate = [] { InvalidateRect(hwnd_client, NULL, FALSE); };
   host.view = [] { return make_editor_view(); };
   host.enter_edit_mode = [] { enter_edit_mode(); };
   host.state_changed = [] { update_engine_ui(); };
   editor_ui::create(hWnd, hInstance, host);

   ShowWindow(hWnd, SW_SHOWMAXIMIZED);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto create_simulation = [&](const std::wstring& file_name)
    {
        std::vector<mass_state> ms;
        std::vector<spring_state> ss;
        std::vector<damper_state> ds;
        const auto loaded = load_simulation(file_name, ms, ss, ds);
        if (!loaded)
            return;   // keep running the current simulation

        runner->stop();
        mass_states = std::move(ms);
        spring_states = std::move(ss);
        damper_states = std::move(ds);
        sim = loaded;
        simulation_clock = 0.0;
        adopt_simulation();
        editing = false;
        editor_ui::set_active(false);
        select_engine(current_engine_kind, false);
        start_runner();
        // Reset zoom and pan when switching simulations
        scale = 1.0;
        pan_x = 0.0;
        pan_y = 0.0;
    };

    auto switch_engine = [&](engine_kind kind)
    {
        runner->stop();
        select_engine(kind, true);
        start_runner();
    };

    hwnd_client = hWnd;
    switch (message)
    {
    case WM_CLOSE:
        if (!editor_ui::confirm_discard())
            break;
        // Stop the worker before tearing down so no GPU work is in flight.
        runner->stop();
        engine.reset();
        DestroyWindow(hWnd);
        break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId) {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                PostMessage(hWnd, WM_CLOSE, 0, 0);
                break;
            case ID_ENGINE_CPU:
                switch_engine(engine_kind::cpu);
                break;
            case ID_ENGINE_CUDA:
                switch_engine(engine_kind::cuda);
                break;
            case ID_EDITOR_RUN:
                toggle_edit_mode();
                break;
            case ID_SIMULATION1:
                create_simulation(L"simulation1.xml");
                break;
            case ID_SIMULATION2:
                create_simulation(L"simulation2.xml");
                break;
            case ID_SIMULATION3:
                create_simulation(L"simulation3.xml");
                break;
            case ID_SIMULATION4:
                create_simulation(L"simulation4.xml");
                break;
            case ID_SIMULATION5:
                create_simulation(L"simulation5.xml");
                break;
            case ID_SIMULATION6:
                create_simulation(L"simulation6.xml");
                break;
            case ID_SIMULATION7:
                create_simulation(L"simulation7.xml");
                break;
            case ID_SIMULATION8:
                create_simulation(L"simulation8.xml");
                break;
            case ID_SIMULATION9:
                create_simulation(L"simulation9.xml");
                break;
            case ID_SIMULATION_SOLAR:
                create_simulation(L"solar_system.xml");
                break;
            case ID_SIMULATION_MILKYWAY:
                create_simulation(L"milky_way.xml");
                break;
            case ID_SPEED_FASTER:
                set_speed(speed_multiplier * 2.0);
                break;
            case ID_SPEED_SLOWER:
                set_speed(speed_multiplier / 2.0);
                break;
            case ID_SPEED_RESET:
                set_speed(speed_base);
                break;
            case ID_SPEED_PAUSE:
                paused = !paused;
                InvalidateRect(hWnd, NULL, FALSE);
                break;
            case ID_VIEW_LABELS:
                labels_visible = !labels_visible;
                InvalidateRect(hWnd, NULL, FALSE);
                break;
            case ID_VIEW_STOP_TRACKING:
                tracked.reset();
                InvalidateRect(hWnd, NULL, FALSE);
                break;
            default:
                if (wmId >= ID_SPEED_PRESET_FIRST && wmId < ID_SPEED_PRESET_FIRST + int(std::size(speed_presets)))
                {
                    set_speed(speed_presets[wmId - ID_SPEED_PRESET_FIRST]);
                    break;
                }
                if (editor_ui::command(wmId))
                    break;
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_INITMENUPOPUP:
        editor_ui::update_menus();
        update_speed_menu();
        update_view_menu();
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_KEYDOWN:
        if (wParam == VK_F5)
        {
            toggle_edit_mode();
            return 0;
        }
        if (wParam == 'L' && !(GetKeyState(VK_CONTROL) & 0x8000))
        {
            SendMessage(hWnd, WM_COMMAND, ID_VIEW_LABELS, 0);
            return 0;
        }
        if (!editing)
        {
            switch (wParam)
            {
            case 'T':
                if (tracked)
                {
                    tracked.reset();
                }
                else
                {
                    POINT at;
                    GetCursorPos(&at);
                    ScreenToClient(hWnd, &at);
                    tracked = mass_at(at.x, at.y);
                }
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            case VK_ESCAPE:
                tracked.reset();
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            case VK_OEM_6:   // ]
                SendMessage(hWnd, WM_COMMAND, ID_SPEED_FASTER, 0);
                return 0;
            case VK_OEM_4:   // [
                SendMessage(hWnd, WM_COMMAND, ID_SPEED_SLOWER, 0);
                return 0;
            case VK_OEM_5:   // backslash
                SendMessage(hWnd, WM_COMMAND, ID_SPEED_RESET, 0);
                return 0;
            case VK_SPACE:
                SendMessage(hWnd, WM_COMMAND, ID_SPEED_PAUSE, 0);
                return 0;
            }
        }
        if (editing && editor_ui::on_key_down(wParam))
            return 0;
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_CONTEXTMENU:
        if (editing)
        {
            POINT at = { (int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam) };
            if (at.x == -1 && at.y == -1)
                GetCursorPos(&at);
            editor_ui::on_context_menu(at.x, at.y);
            break;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            if (rc.right > rc.left && rc.bottom > rc.top)
                paint_scene(hdc, rc);
            EndPaint(hWnd, &ps);
            update_fps();
            runner->frame_done();
        }
        break;
    case WM_USER + 2:
        {
            // The active engine failed (e.g. a CUDA error); continue on the CPU from the last snapshot.
            runner->stop();
            std::string message_text;
            EnterCriticalSection(&cs);
            message_text = engine_failure_message;
            LeaveCriticalSection(&cs);
            MessageBoxA(hWnd, message_text.c_str(), "Engine failed, switching to CPU", MB_OK | MB_ICONERROR);
            select_engine(engine_kind::cpu, false);
            start_runner();
        }
        break;
    case WM_SIZE:
    {
        if (g_pRenderTarget)
        {
            // Resize render target
            UINT32 width = LOWORD(lParam);
            UINT32 height = HIWORD(lParam);
            if (width > 0 && height > 0)
            {
                DiscardDeviceResources();
                CreateDeviceResources(hWnd);
            }
        }
    }
    break;
    case WM_MOUSEWHEEL:
    {
        const auto delta = GET_WHEEL_DELTA_WPARAM(wParam);

        RECT rc;
        GetClientRect(hWnd, &rc);
        const int width = rc.right - rc.left;
        const int height = rc.bottom - rc.top;
        if (width <= 0 || height <= 0)
            break;

        // Mouse position is in screen coords in lParam for WM_MOUSEWHEEL, convert to client
        POINT ptScreen;
        ptScreen.x = (int)(SHORT)LOWORD(lParam);
        ptScreen.y = (int)(SHORT)HIWORD(lParam);
        POINT ptClient = ptScreen;
        ScreenToClient(hWnd, &ptClient);

        const int mx = ptClient.x;
        const int my = ptClient.y;

        const double aspect_ratio = double(width) / double(height);

        // Current world bounds (before zoom)
        const auto bounds = current_bounds();
        const double xmin_world = scale * bounds.xmin * aspect_ratio + pan_x;
        const double xmax_world = scale * bounds.xmax * aspect_ratio + pan_x;
        const double ymin_world = scale * bounds.ymin + pan_y;
        const double ymax_world = scale * bounds.ymax + pan_y;

        const double world_x_at_cursor = xmin_world + (double(mx) * (xmax_world - xmin_world) / double(width));
        const double world_y_at_cursor = ymin_world + (double(my) * (ymax_world - ymin_world) / double(height));

        // Apply zoom
        if (delta > 0)
            scale *= 0.9;
        else if (delta < 0)
            scale *= 1.1;

        // Recompute pan so the world point under cursor remains stationary
        const double new_x_component = scale * aspect_ratio * (bounds.xmin + (double(mx) * (bounds.xmax - bounds.xmin) / double(width)));
        const double new_y_component = scale * (bounds.ymin + (double(my) * (bounds.ymax - bounds.ymin) / double(height)));

        pan_x = world_x_at_cursor - new_x_component;
        pan_y = world_y_at_cursor - new_y_component;

        InvalidateRect(hWnd, NULL, FALSE);
    }
    break;
    case WM_LBUTTONDBLCLK:
        if (editing)
            return WndProc(hWnd, WM_LBUTTONDOWN, wParam, lParam);
        // Double-click a mass to follow it, or empty space to let go.
        tracked = mass_at((int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam));
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    case WM_LBUTTONDOWN:
        if (editing)
        {
            editor_ui::on_mouse_down((int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam), wParam);
            break;
        }
        [[fallthrough]];
    case WM_MBUTTONDOWN:
    {
        SetCapture(hWnd);
        panning = true;
        pan_start_pt.x = (int)(SHORT)LOWORD(lParam);
        pan_start_pt.y = (int)(SHORT)HIWORD(lParam);
        pan_start_x = pan_x;
        pan_start_y = pan_y;
    }
    break;
    case WM_MOUSEMOVE:
    {
        if (panning)
        {
            const int x = (int)(SHORT)LOWORD(lParam);
            const int y = (int)(SHORT)HIWORD(lParam);
            const int dx = x - pan_start_pt.x;
            const int dy = y - pan_start_pt.y;

            // Dragging the view lets go of a tracked mass, but a click's jitter does not.
            if (tracked)
            {
                if (dx * dx + dy * dy < 16)
                    break;
                tracked.reset();
            }

            RECT rc;
            GetClientRect(hWnd, &rc);
            const int screen_w = rc.right - rc.left;
            const int screen_h = rc.bottom - rc.top;
            if (screen_w <= 0 || screen_h <= 0) break;

            const auto aspect_ratio = double(screen_w) / double(screen_h);
            const auto bounds = current_bounds();
            const double world_width = scale * (bounds.xmax - bounds.xmin) * aspect_ratio;
            const double world_height = scale * (bounds.ymax - bounds.ymin);

            pan_x = pan_start_x - dx * (world_width / double(screen_w));
            pan_y = pan_start_y - dy * (world_height / double(screen_h));

            // request repaint
            InvalidateRect(hWnd, NULL, FALSE);
        }
        else if (editing)
        {
            editor_ui::on_mouse_move((int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam), wParam);
        }
    }
    break;
    case WM_LBUTTONUP:
        if (editing)
            editor_ui::on_mouse_up((int)(SHORT)LOWORD(lParam), (int)(SHORT)HIWORD(lParam), wParam);
        [[fallthrough]];
    case WM_MBUTTONUP:
    {
        if (panning)
        {
            panning = false;
            ReleaseCapture();
        }
    }
    break;
    case WM_DESTROY:
        // Clean up persistent GDI objects
        if (g_hGreenPen) { DeleteObject(g_hGreenPen); g_hGreenPen = NULL; }
        if (g_hRedBrush) { DeleteObject(g_hRedBrush); g_hRedBrush = NULL; }
        if (g_hBlueBrush) { DeleteObject(g_hBlueBrush); g_hBlueBrush = NULL; }

        // Clean up backbuffer
        if (g_hBackBitmap)
        {
            if (g_hOldBackBitmap)
                SelectObject(g_hMemDC, g_hOldBackBitmap);
            DeleteObject(g_hBackBitmap);
            g_hBackBitmap = NULL;
            g_hOldBackBitmap = NULL;
        }
        if (g_hMemDC)
        {
            DeleteDC(g_hMemDC);
            g_hMemDC = NULL;
        }

        // Clean up Direct2D
        DiscardDeviceResources();
        SafeRelease(&g_pTextFormat);
        SafeRelease(&g_pLabelFormat);
        SafeRelease(&g_pDWriteFactory);
        SafeRelease(&g_pD2DFactory);

        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
