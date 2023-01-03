// physics.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "physics.h"
#include "simulation.h"
#include "coordinate_translation.h"

#include <process.h>
#include <string>
#include <memory>

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

void paint_scene(const HWND& hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWnd, &ps);

    RECT rc;
    GetClientRect(hWnd, &rc);

    const auto aspect_ratio = double(rc.right - rc.left) / double(rc.bottom - rc.top);

    viewport vp(rc, scale * sim->xmin() * aspect_ratio, scale * sim->xmax() * aspect_ratio, scale * sim->ymin(), scale * sim->ymax());

    HDC hmemDC = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    SelectObject(hmemDC, bmp);

    auto pen = CreatePen(0, 1, RGB(0, 255, 0));
    SelectObject(hmemDC, pen);

    auto red_brush = CreateSolidBrush(RGB(255, 0, 0));
    auto blue_brush = CreateSolidBrush(RGB(0, 0, 255));
    SelectObject(hmemDC, blue_brush);

    EnterCriticalSection(&cs);
    const auto& sim_copy = *sim;
    auto mass_states_copy = mass_states;
    LeaveCriticalSection(&cs);
    for (int i = 0; i < sim_copy.model().springs().size(); ++i)
    {
        if (spring_states[i].broken_)
            continue;

        const auto& spring = sim_copy.model().springs()[i];
        const auto& p1 = mass_states_copy[spring.id_mass1()].position_;
        const auto& p2 = mass_states_copy[spring.id_mass2()].position_;

        const auto x1 = vp.x_to_screen(p1.x());
        const auto y1 = vp.y_to_screen(p1.y());
        const auto x2 = vp.x_to_screen(p2.x());
        const auto y2 = vp.y_to_screen(p2.y());

        MoveToEx(hmemDC, x1, y1, NULL);
        LineTo(hmemDC, x2, y2);
    }

    for (int i = 0; i < sim_copy.model().masses().size(); ++i)
    {
        const auto& m = sim_copy.model().masses()[i];
        const auto& position = mass_states[i].position_;

        const auto left = vp.x_to_screen(position.x() - m.r()) - 1;
        const auto top = vp.y_to_screen(position.y() - m.r()) - 1;
        const auto right = vp.x_to_screen(position.x() + m.r()) + 1;
        const auto bottom = vp.y_to_screen(position.y() + m.r()) + 1;

        if (mass_states_copy[i].fixed_)
        {
            SelectObject(hmemDC, blue_brush);
        }
        else
        {
            SelectObject(hmemDC, red_brush);
        }

        if (mass_states_copy[i].fixed_)
        {
            //Rectangle(hmemDC, left, top, right, bottom);
            Ellipse(hmemDC, left, top, right, bottom);
        }
        else
        {
            Ellipse(hmemDC, left, top, right, bottom);
        }
    }

    //EnterCriticalSection(&cs);
    mass_states = mass_states_copy;
    //LeaveCriticalSection(&cs);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, hmemDC, 0, 0, SRCCOPY);

    DeleteObject(red_brush);
    DeleteObject(blue_brush);
    DeleteObject(pen);

    DeleteObject(bmp);
    DeleteDC(hmemDC);

    EndPaint(hWnd, &ps);
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

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   hwnd_client = hWnd;

   InitializeCriticalSection(&cs);

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
        scale = 1.0;
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
        InvalidateRect(hWnd, &rc, FALSE);
        UpdateWindow(hWnd);
    }
        break;
    case WM_PAINT:
        paint_scene(hWnd);
        break;
    case WM_SIZE:
    {
        
    }
    break;
    case WM_MOUSEWHEEL:
    {
        const auto delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta > 0)
        {
            scale *= 0.9;
        }
        else if (delta < 0)
        {
            scale *= 1.1;
        }
    }
    break;
    case WM_DESTROY:
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
