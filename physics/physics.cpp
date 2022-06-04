// physics.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "physics.h"
#include "simulation.h"
#include "coordinate_translation.h"

#include <process.h>
#include <string>



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

std::vector<mass_state> mass_states;
std::vector<spring_state> spring_states;
std::vector<damper_state> damper_states;

simulation create_simulation_1()
{
    std::vector<mass> masses;
    simulation::add_mass(
        constants::earth_mass, 
        constants::earth_radius, 
        {0.0, 0.0}, 
        {}, 
        {}, 
        false,
        masses, 
        mass_states);

    simulation::add_mass(
        constants::earth_mass,
        constants::earth_radius,
        { -20.0 * constants::earth_radius, constants::earth_radius * 38.0 },
        {500.0, 300.0},
        {},
        false,
        masses,
        mass_states);

    simulation::add_mass(
        constants::earth_mass * 0.6f,
        constants::earth_radius * 0.2f,
        { 30.0 * constants::earth_radius, -constants::earth_radius * 48.0 },
        { -900.0, -600.0 },
        {},
        false,
        masses,
        mass_states);

    simulation::add_mass(
        constants::lunar_mass, 
        constants::lunar_radius, 
        { -constants::earth_radius * 5, constants::earth_radius * 5 }, 
        {-1700, -2700}, 
        {}, 
        false,
        masses,
        mass_states);
    
    simulation::add_mass(
        constants::lunar_mass, 
        constants::lunar_radius, 
        { -constants::earth_radius * 6.5, constants::earth_radius * 3 }, 
        { -1900, -3200 }, 
        {}, 
        false,
        masses,
        mass_states);

    simulation::add_mass(
        constants::lunar_mass, 
        constants::lunar_radius, 
        { -constants::earth_radius * 7, constants::earth_radius * 5 }, 
        { -1900, -1200 }, 
        {}, 
        false,
        masses,
        mass_states);

    simulation::add_mass(
        75.0, 
        10000, 
        { constants::earth_radius + 200000.0, constants::earth_radius + 200000.0}, 
        {5500, -3700}, 
        {}, 
        false,
        masses,
        mass_states);

    simulation::add_mass(
        9000, 
        40000, 
        { -4 * constants::earth_radius -200000.0, -6 * constants::earth_radius }, 
        { 3200, 0 }, 
        {}, 
        false,
        masses,
        mass_states);

    std::vector<spring> springs;
    
    std::vector<damper> dampers;

    return { model_system(masses, springs, dampers), -300000000, 700000000, -300000000, 700000000, 1, 240 };
}

simulation create_simulation_2()
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    simulation::add_mass(constants::earth_mass * 0.001f, 20.0f, { 0.0f, constants::earth_radius }, { 0.0f, 0.0f }, {}, false, masses, mass_states);

    const auto m0 = simulation::add_mass(100, 20, { 500, 100},  {-3,0}, {}, false, masses, mass_states);
    const auto m1 = simulation::add_mass(100, 20, { 600, 100},  {}, {}, false, masses, mass_states);
    const auto m2 = simulation::add_mass(100, 20, { 500, 200 }, {}, {}, false, masses, mass_states);
    const auto m3 = simulation::add_mass(100, 20, { 600, 200 }, {}, {}, false, masses, mass_states);
    const auto m4 = simulation::add_mass(50,  20, { 550, 150 }, {}, {}, false, masses, mass_states);

    simulation::add_spring(m0, m1, true, 10, 100, springs, spring_states);
    simulation::add_spring(m0, m2, true, 10, 100, springs, spring_states);
    simulation::add_spring(m1, m3, true, 10, 100, springs, spring_states);
    simulation::add_spring(m2, m3, true, 10, 100, springs, spring_states);
    simulation::add_spring(m0, m4, true, 10, 75,  springs, spring_states);
    simulation::add_spring(m1, m4, true, 10, 75,  springs, spring_states);
    simulation::add_spring(m2, m4, true, 10, 75,  springs, spring_states);
    simulation::add_spring(m3, m4, true, 10, 75,  springs, spring_states);

    //simulation::add_damper(m0, m1, 0.6, 700, dampers);
    //simulation::add_damper(m0, m2, 0.6, 700, dampers);
    //simulation::add_damper(m1, m3, 0.6, 700, dampers);
    //simulation::add_damper(m2, m3, 0.6, 700, dampers);
    
    //simulation::add_damper(m0, m4, 0.1, 700, dampers);
    //simulation::add_damper(m1, m4, 0.1, 700, dampers);
    //simulation::add_damper(m2, m4, 0.1, 700, dampers);
    //simulation::add_damper(m3, m4, 0.1, 700, dampers);

    return { model_system(masses, springs, dampers), -10000, 10000, -10000, 10000, 0.1, 10 };
}

simulation create_simulation_3()
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    size_t rows = 25;
    size_t cols = 41;

    float xdist = 100;
    float ydist = 100;
    float ddist = sqrt(xdist * xdist + ydist * ydist);
    float radius = 25;
    float ks = 10;

    for (size_t r = 0; r < rows; ++r)
    {
        float y = -3500 + (float)r * xdist;
        for (size_t c = 0; c < cols; ++c)
        {
            float x = -7000 + (float)c * ydist;

            const auto fixed = (r < 4 && (c < cols / 4 || c > 3 * cols / 5));

            const auto mass_id = r <  rows / 2 ?
                simulation::add_mass(10., radius, double2{ x, y }, double2{0, 0}, double2{}, fixed, masses, mass_states)
                : simulation::add_mass(43., radius, double2{ x, y }, double2{0, 0}, double2{}, fixed, masses, mass_states);

            if (r < rows - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r + 1) * cols + c, true, (float)rows - r, ydist, springs, spring_states);
                simulation::add_damper(mass_id, (r + 1) * cols + c, springs[spring_id], dampers, damper_states);
            }
            if (c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r) * cols + c + 1, true, 5, xdist, springs, spring_states);
                simulation::add_damper(mass_id, (r) * cols + c + 1, springs[spring_id], dampers, damper_states);
            }
            if (r < rows - 1 && c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r + 1) * cols + c + 1, true, (float)rows - r, ddist, springs, spring_states);
                simulation::add_damper(mass_id, (r + 1) * cols + c + 1, springs[spring_id], dampers, damper_states);
            }
            if (r > 0 && c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r - 1) * cols + c + 1, true, 5, ddist, springs, spring_states);
                simulation::add_damper(mass_id, (r - 1) * cols + c + 1, springs[spring_id], dampers, damper_states);
            }
        }
    }
    simulation::add_mass(constants::earth_mass * 0.05f, 20.0f, { 0.0f, constants::earth_radius }, { 0.0f, 0.0f }, {}, false, masses, mass_states);

    return { model_system(masses, springs, dampers), -4000, 4000, -4000, 4000, 0.1, 10 };
}

simulation create_simulation_4()
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    const auto m = constants::lunar_mass * 0.25;
    const auto r = constants::lunar_radius;

    for (float x = -15; x < 1; ++x)
    {
        for (float y = -15; y < 15; ++y)
        {
            simulation::add_mass(m, r, { (x ) * -40 * constants::lunar_radius, (y - 20) * -40 * constants::lunar_radius }, {600 - x * 150, -500 - y * 50}, {}, false, masses, mass_states);

            simulation::add_mass(m, r, { (x ) * 40 * constants::lunar_radius, (y - 20) * 40 * constants::lunar_radius }, { -500 + x * 50,  400 + y * 120 }, {}, false, masses, mass_states);
        }
    }

    simulation::add_mass(constants::earth_mass * 5.0f, r * 8, { 0.0f, 0.0f }, { -500.0f, 0.0f }, {}, false, masses, mass_states);
    simulation::add_mass(constants::earth_mass * 1.f, r * 6, { -constants::earth_radius * 40, constants::earth_radius * 90 }, { 1200.0f, 400.0f }, {}, false, masses, mass_states);

    return { model_system(masses, springs, dampers), -50 * 30 * constants::lunar_radius, 50 * 30 * constants::lunar_radius, -50 * 30 * constants::lunar_radius, 50 * 30 * constants::lunar_radius, 1200, 1 };
}

simulation create_simulation_5()
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;


    const auto m0 = simulation::add_mass(10, 10, { 500.0, 100.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }, true, masses, mass_states);
    const auto m1 = simulation::add_mass(10, 10, { 500.0, 800.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }, false, masses, mass_states);
    simulation::add_spring(m0, m1, true, 100, 800, springs, spring_states);

    return { model_system(masses, springs, dampers),  200, 1000, 200, 1000, 0.001, 10};
}

simulation create_simulation_6()
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    size_t rows = 16;
    size_t cols = 16;

    float xdist = 100;
    float ydist = 100;
    float ddist = sqrt(xdist * xdist + ydist * ydist);
    float radius = 12;
    float ks = 10;

    for (size_t r = 0; r < rows; ++r)
    {
        float y = -1600 + (float)r * xdist;
        for (size_t c = 0; c < cols; ++c)
        {
            float x = -2000 + (float)c * ydist;

            const auto fixed = false;

            const auto mass_id = r < rows / 2 ?
                simulation::add_mass(1., radius, double2{ x, y }, double2{ 7, 0 }, double2{}, fixed, masses, mass_states)
                : simulation::add_mass(1., radius, double2{ x, y }, double2{ 0, 0 }, double2{}, fixed, masses, mass_states);

            if (r < rows - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r + 1) * cols + c, true, 2, ydist, springs, spring_states);
                simulation::add_damper(mass_id, (r + 1) * cols + c, springs[spring_id], dampers, damper_states);
            }
            if (c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r)*cols + c + 1, true, 2, xdist, springs, spring_states);
                simulation::add_damper(mass_id, (r)*cols + c + 1, springs[spring_id], dampers, damper_states);
            }
            if (r < rows - 1 && c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r + 1) * cols + c + 1, true, 2, ddist, springs, spring_states);
                simulation::add_damper(mass_id, (r + 1) * cols + c + 1, springs[spring_id], dampers, damper_states);
            }
            if (r > 0 && c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r - 1) * cols + c + 1, true, 2, ddist, springs, spring_states);
                simulation::add_damper(mass_id, (r - 1) * cols + c + 1, springs[spring_id], dampers, damper_states);
            }
        }
    }
    simulation::add_mass(constants::earth_mass * 0.015f, 20.0f, { 0.0f, constants::earth_radius }, { 0.0f, 0.0f }, {}, false, masses, mass_states);

    return { model_system(masses, springs, dampers), -4000, 5000, -4000, 5000, 0.1, 10 };
}


simulation sim = create_simulation_3();

float clock = 0.0;
HWND hwnd_client = 0;
CRITICAL_SECTION cs;
unsigned threadid;
HANDLE hthread;
bool exitthread = false;

unsigned __stdcall simulation_thread(void* p)
{
    while (!exitthread)
    {
        const auto iterations_per_update = sim.iterations_per_update();
        const auto dt = (float)sim.dt();

        EnterCriticalSection(&cs);
        for (size_t i = 0; i < iterations_per_update; ++i)
        {
            sim.prepare_for_update(mass_states);
            sim.update_broken_springs_and_dampers(mass_states, spring_states, damper_states);
            sim.update_gravitational_force(mass_states);
            sim.update_spring_force(mass_states, spring_states);
            sim.update_damper_force(mass_states, damper_states);
            sim.update_spatial(mass_states, dt);
            RECT rc;
            GetClientRect(hwnd_client, &rc);
            sim.update_floor((float)sim.ymax(), mass_states);
            clock += dt;
        }
        LeaveCriticalSection(&cs);
        Sleep(0);
        if (!exitthread)
            SendMessage(hwnd_client, WM_USER + 1, 0, 0);
    }
    return 0;
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
    hwnd_client = hWnd;
    switch (message)
    {
    case WM_CLOSE:
        exitthread = true;
        WaitForSingleObject(hthread, INFINITE);
        DestroyWindow(hWnd);
        break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                exitthread = true;
                WaitForSingleObject(hthread, INFINITE);
               
                DestroyWindow(hWnd);
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
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            
            RECT rc;
            GetClientRect(hWnd, &rc);

            const auto aspect_ratio = float(rc.right - rc.left) / float(rc.bottom - rc.top);

            viewport vp(rc, sim.xmin() * aspect_ratio, sim.xmax() * aspect_ratio, sim.ymin(), sim.ymax());

            HDC hmemDC = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            SelectObject(hmemDC, bmp);

            auto pen = CreatePen(0, 1, RGB(0, 255, 0));
            SelectObject(hmemDC, pen);

            auto brush = CreateSolidBrush(RGB(255, 0, 0));
            SelectObject(hmemDC, brush);

            EnterCriticalSection(&cs);
            const auto& sim_copy = sim;
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

                const auto left = vp.x_to_screen(position.x()   - m.r()) - 1;
                const auto top = vp.y_to_screen(position.y()    - m.r()) - 1;
                const auto right = vp.x_to_screen(position.x()  + m.r()) + 1;
                const auto bottom = vp.y_to_screen(position.y() + m.r()) + 1;

                Ellipse(hmemDC, left, top, right, bottom);
            }

            EnterCriticalSection(&cs);
            mass_states = mass_states_copy;
            LeaveCriticalSection(&cs);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, hmemDC, 0, 0, SRCCOPY);

            DeleteObject(brush);
            DeleteObject(pen);

            DeleteObject(bmp);
            DeleteDC(hmemDC);

            EndPaint(hWnd, &ps);
        }
        break;
    case WM_SIZE:
    {
        
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
