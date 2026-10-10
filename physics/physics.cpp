// physics.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "physics.h"
#include "simulation.h"
#include "coordinate_translation.h"
                                                                    
#include <process.h>
#include <string>
#include <memory>
#include <d2d1.h>
#pragma comment(lib, "d2d1")

#include "simulation_examples.h"

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

double simulation_clock = 0.0;
HWND hwnd_client = 0;
CRITICAL_SECTION cs;
unsigned threadid;
HANDLE hthread;
bool exitthread = false;

std::vector<mass_state> mass_states;
std::vector<spring_state> spring_states;
std::vector<damper_state> damper_states;

std::shared_ptr<simulation> sim = create_simulation_1(mass_states, spring_states, damper_states);

// Direct2D globals
ID2D1Factory*        g_pD2DFactory = nullptr;
ID2D1HwndRenderTarget* g_pRenderTarget = nullptr;
ID2D1SolidColorBrush*  g_pBrushGreen = nullptr;
ID2D1SolidColorBrush*  g_pBrushRed = nullptr;
ID2D1SolidColorBrush*  g_pBrushBlue = nullptr;

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
    }

    return hr;
}

void DiscardDeviceResources()
{
    SafeRelease(&g_pBrushGreen);
    SafeRelease(&g_pBrushRed);
    SafeRelease(&g_pBrushBlue);
    SafeRelease(&g_pRenderTarget);
}

unsigned __stdcall simulation_thread(void* p)
{
    while (!exitthread)
    {
        RECT rc;
        GetClientRect(hwnd_client, &rc);

        const auto iterations_per_update = sim->iterations_per_update();
        const auto dt = (double)sim->dt();

        EnterCriticalSection(&cs);
        for (size_t i = 0; i < iterations_per_update; ++i)
        {
            sim->prepare_for_update(mass_states);
            sim->update_broken_springs_and_dampers(mass_states, spring_states, damper_states);
            sim->update_gravitational_force(mass_states);
            sim->gravitational_vector_field({});
            //sim->update_repulsive_force(mass_states);
            sim->update_spring_force(mass_states, spring_states);
            sim->update_damper_force(mass_states, damper_states);
            sim->update_spatial(mass_states, dt);
            sim->update_collisions(mass_states);
            sim->update_floor(mass_states);
            
            simulation_clock += dt;
        }
        LeaveCriticalSection(&cs);
        if (exitthread)
            ExitThread(0);
        else
            SendMessage(hwnd_client, WM_USER + 1, 0, 0);
    }
    return 0;
}

void paint_scene(const HDC hdc, RECT& rc)
{
    const auto aspect_ratio = double(rc.right - rc.left) / double(rc.bottom - rc.top);

    // apply pan offsets to world bounds
    const double xmin_world = scale * sim->xmin() * aspect_ratio + pan_x;
    const double xmax_world = scale * sim->xmax() * aspect_ratio + pan_x;
    const double ymin_world = scale * sim->ymin() + pan_y;
    const double ymax_world = scale * sim->ymax() + pan_y;

    viewport vp(rc, xmin_world, xmax_world, ymin_world, ymax_world);

    // copy shared state under lock
    EnterCriticalSection(&cs);
    const auto sim_copy = *sim;
    const auto mass_states_copy = mass_states;
    const auto spring_states_copy = spring_states;
    LeaveCriticalSection(&cs);

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

            g_pRenderTarget->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), g_pBrushGreen, 1.0f);
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
            D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), r_screen, r_screen);

            ID2D1Brush* fillBrush = mass_states_copy[i].fixed_ ? g_pBrushBlue : g_pBrushRed;
            if (fillBrush)
                g_pRenderTarget->FillEllipse(ellipse, fillBrush);

            if (g_pBrushGreen)
                g_pRenderTarget->DrawEllipse(ellipse, g_pBrushGreen, 1.0f);
        }

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

            MoveToEx(hmemDC, x1, y1, NULL);
            LineTo(hmemDC, x2, y2);
        }

        // Draw masses: select fill brush then draw ellipse with green pen outline
        for (size_t i = 0; i < sim_copy.model().masses().size(); ++i)
        {
            const auto& m = sim_copy.model().masses()[i];
            const auto& pos = mass_states_copy[i].position_;

            HBRUSH hFill = mass_states_copy[i].fixed_ ? hBlue : hRed;
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hmemDC, hFill);

            const int left = vp.x_to_screen(pos.x() - m.r()) - 1;
            const int top = vp.y_to_screen(pos.y() - m.r()) - 1;
            const int right = vp.x_to_screen(pos.x() + m.r()) + 1;
            const int bottom = vp.y_to_screen(pos.y() + m.r()) + 1;

            Ellipse(hmemDC, left, top, right, bottom);

            // restore brush
            SelectObject(hmemDC, hOldBrush);
        }

        // restore original pen
        SelectObject(hmemDC, hOldPen);
        if (hLocalPen)
            DeleteObject(hLocalPen);

        // cleanup
        DeleteObject(hRed);
        DeleteObject(hBlue);

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

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
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

   hthread = (HANDLE)_beginthreadex(NULL, 0, &simulation_thread, NULL, 0, &threadid);

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
    auto create_simulation = [&](const auto& sim_fn, auto& ms, auto& ss, auto& ds)
    {
        exitthread = true;
        WaitForSingleObject(hthread, INFINITE);
        mass_states.clear();
        spring_states.clear();
        damper_states.clear();
        sim = sim_fn(ms, ss, ds);
        exitthread = false;
        hthread = (HANDLE)_beginthreadex(NULL, 0, &simulation_thread, NULL, 0, &threadid);
        // Reset zoom and pan when switching simulations
        scale = 1.0;
        pan_x = 0.0;
        pan_y = 0.0;
    };

    hwnd_client = hWnd;
    switch (message)
    {
    case WM_CLOSE:
        TerminateProcess(GetCurrentProcess(), 0);
        break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId) {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                TerminateProcess(GetCurrentProcess(), 0);
                break;
            case ID_SIMULATION1:
                create_simulation(create_simulation_1, mass_states, spring_states, damper_states);
                break;
            case ID_SIMULATION2:
                create_simulation(create_simulation_2, mass_states, spring_states, damper_states);
                break;
            case ID_SIMULATION3:
                create_simulation(create_simulation_3, mass_states, spring_states, damper_states);
                break;
            case ID_SIMULATION4:
                create_simulation(create_simulation_4, mass_states, spring_states, damper_states);
                break;
            case ID_SIMULATION5:
                create_simulation(create_simulation_5, mass_states, spring_states, damper_states);
                break;
            case ID_SIMULATION6:
                create_simulation(create_simulation_6, mass_states, spring_states, damper_states);
                break;
            case ID_SIMULATION7:
                create_simulation(create_simulation_7, mass_states, spring_states, damper_states);
                break;
            case ID_SIMULATION8:
                create_simulation(create_simulation_8, mass_states, spring_states, damper_states);
                break;
            case ID_SIMULATION9:
                create_simulation(create_simulation_9, mass_states, spring_states, damper_states);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_USER + 1:
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            HDC hdc = GetDC(hWnd);
            paint_scene(hdc, rc);
            ReleaseDC(hWnd, hdc);
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
        const double xmin_world = scale * sim->xmin() * aspect_ratio + pan_x;
        const double xmax_world = scale * sim->xmax() * aspect_ratio + pan_x;
        const double ymin_world = scale * sim->ymin() + pan_y;
        const double ymax_world = scale * sim->ymax() + pan_y;

        const double world_x_at_cursor = xmin_world + (double(mx) * (xmax_world - xmin_world) / double(width));
        const double world_y_at_cursor = ymin_world + (double(my) * (ymax_world - ymin_world) / double(height));

        // Apply zoom
        if (delta > 0)
            scale *= 0.9;
        else if (delta < 0)
            scale *= 1.1;

        // Recompute pan so the world point under cursor remains stationary
        const double new_x_component = scale * aspect_ratio * (sim->xmin() + (double(mx) * (sim->xmax() - sim->xmin()) / double(width)));
        const double new_y_component = scale * (sim->ymin() + (double(my) * (sim->ymax() - sim->ymin()) / double(height)));

        pan_x = world_x_at_cursor - new_x_component;
        pan_y = world_y_at_cursor - new_y_component;

        PostMessage(hWnd, WM_USER + 1, 0, 0);
    }
    break;
    case WM_LBUTTONDOWN:
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

            RECT rc;
            GetClientRect(hWnd, &rc);
            const int screen_w = rc.right - rc.left;
            const int screen_h = rc.bottom - rc.top;
            if (screen_w <= 0 || screen_h <= 0) break;

            const auto aspect_ratio = double(screen_w) / double(screen_h);
            const double world_width = scale * (sim->xmax() - sim->xmin()) * aspect_ratio;
            const double world_height = scale * (sim->ymax() - sim->ymin());

            pan_x = pan_start_x - dx * (world_width / double(screen_w));
            pan_y = pan_start_y - dy * (world_height / double(screen_h));

            // request repaint
            PostMessage(hWnd, WM_USER + 1, 0, 0);
        }
    }
    break;
    case WM_LBUTTONUP:
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
