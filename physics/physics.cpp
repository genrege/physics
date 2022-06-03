// physics.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "physics.h"
#include "simulation.h"
#include "coordinate_translation.h"

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
    const auto spring1_id = simulation::add_spring(0, 1, true, 1e17, 5 * constants::earth_radius, springs, spring_states);
    const auto spring2_id = simulation::add_spring(1, 2, true, 1e16, 4 * constants::earth_radius, springs, spring_states);
    
    std::vector<damper> dampers;
    simulation::add_damper(0, 1, 1e9, 5 * constants::earth_radius, springs[spring1_id], dampers, damper_states);
    simulation::add_damper(1, 2, 1e6, 5 * constants::earth_radius, springs[spring2_id], dampers, damper_states);

    return model_system(masses, springs, dampers);
}

simulation create_simulation_2()
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    simulation::add_mass(constants::earth_mass * 0.001, 20, { 0.0, constants::earth_radius }, { 0,0 }, {}, false, masses, mass_states);

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

    return model_system(masses, springs, dampers);
}

simulation create_simulation_3()
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    size_t rows = 6;
    size_t cols = 32;

    double xdist = 200;
    double ydist = 200;
    double ddist = sqrt(xdist * xdist + ydist * ydist);
    double radius = 50;
    double ks = 10;

    for (size_t r = 0; r < rows; ++r)
    {
        double y = -2500 + (double)r * xdist;
        for (size_t c = 0; c < cols; ++c)
        {
            double x = 400 + (double)c * ydist;

            const auto fixed = (r == 0 && c == 0);

            const auto mass_id = r <  rows / 2 ?
                simulation::add_mass(1000, radius, { x, y }, {0, 0}, {}, fixed, masses, mass_states)
                : simulation::add_mass(1000, radius, { x, y }, {0, 0}, {}, fixed, masses, mass_states);

            if (r < rows - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r + 1) * cols + c, true, 4, ydist, springs, spring_states);
                simulation::add_damper(mass_id, (r + 1) * cols + c, ks, 100, springs[spring_id], dampers, damper_states);
            }
            if (c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r) * cols + c + 1, true, 4, xdist, springs, spring_states);
                simulation::add_damper(mass_id, (r) * cols + c + 1, ks, 100, springs[spring_id], dampers, damper_states);
            }
            if (r < rows - 1 && c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r + 1) * cols + c + 1, true, 4, ddist, springs, spring_states);
                simulation::add_damper(mass_id, (r + 1) * cols + c + 1, ks, 100, springs[spring_id], dampers, damper_states);
            }
            if (r > 0 && c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r - 1) * cols + c + 1, true, 4, ddist, springs, spring_states);
                simulation::add_damper(mass_id, (r - 1) * cols + c + 1, ks, 100, springs[spring_id], dampers, damper_states);
            }
        }
    }
    simulation::add_mass(constants::earth_mass * 0.001, 20, { 0.0, constants::earth_radius }, { 0,0 }, {}, false, masses, mass_states);

    return model_system(masses, springs, dampers);
}

simulation create_simulation_4()
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    const auto m0 = simulation::add_mass(100, 20, { 200, 100 }, {}, {}, false, masses, mass_states);
    const auto m1 = simulation::add_mass(100, 20, { 800, 100 }, {}, {}, false, masses, mass_states);

    const auto spring_id = simulation::add_spring(m0, m1, true, 10, 400, springs, spring_states);
    simulation::add_damper(m0, m1, 0.14, 100, springs[spring_id], dampers, damper_states);

    return model_system(masses, springs, dampers);
}

simulation sim = create_simulation_3();

double clock = 0.0;

void update_simulation(HWND hwnd, UINT, UINT_PTR, DWORD)
{
    size_t iterations_per_update = 2;
    const double dt = 0.5;

    for (size_t i = 0; i < iterations_per_update; ++i)
    {
        sim.prepare_for_update(mass_states);
        sim.update_broken_springs_and_dampers(mass_states, spring_states, damper_states);
        sim.update_gravitational_force(mass_states);
        sim.update_spring_force(mass_states, spring_states);
        sim.update_damper_force(mass_states, damper_states);
        sim.update_spatial(mass_states, dt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        sim.update_floor((double)rc.bottom * 4 - 20, mass_states);
        clock += dt;
    }

    InvalidateRect(hwnd, NULL, FALSE);
    UpdateWindow(hwnd);

    static bool first = true;
    if (first)
    {
        first = false;
        Sleep(2000);
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

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, SW_SHOWMAXIMIZED);
   UpdateWindow(hWnd);

   SetTimer(hWnd, 1, 1, update_simulation);

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
    switch (message)
    {
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
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            
            RECT rc;
            GetClientRect(hWnd, &rc);

            const auto aspect_ratio = double(rc.right - rc.left) / double(rc.bottom - rc.top);

            //this one for gravity sim 
            //viewport vp(rc, -70000000 * aspect_ratio, 70000000 * aspect_ratio, -70000000.0, 70000000.0);

            //this one for spring system sim
            viewport vp(rc, -rc.right * 4, rc.right * 4, -rc.bottom * 4, rc.bottom * 4);

            HDC hmemDC = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            SelectObject(hmemDC, bmp);

            auto pen = CreatePen(0, 1, RGB(0, 255, 0));
            SelectObject(hmemDC, pen);

            auto brush = CreateSolidBrush(RGB(255, 0, 0));
            SelectObject(hmemDC, brush);

            for (int i = 0; i < sim.model().springs().size(); ++i)
            {
                if (spring_states[i].broken_)
                    continue;

                const auto& spring = sim.model().springs()[i];
                const auto& p1 = mass_states[spring.id_mass1()].position_;
                const auto& p2 = mass_states[spring.id_mass2()].position_;

                const auto x1 = vp.x_to_screen(p1.x());
                const auto y1 = vp.y_to_screen(p1.y());
                const auto x2 = vp.x_to_screen(p2.x());
                const auto y2 = vp.y_to_screen(p2.y());

                MoveToEx(hmemDC, x1, y1, NULL);
                LineTo(hmemDC, x2, y2);
            }

            for (int i = 0; i < sim.model().masses().size(); ++i)
            {
                continue;
                const auto& m = sim.model().masses()[i];
                const auto& position = mass_states[i].position_;

                const auto left = vp.x_to_screen(position.x()   - 0.1 * m.r()) - 1;
                const auto top = vp.y_to_screen(position.y()    - 0.1 * m.r()) - 1;
                const auto right = vp.x_to_screen(position.x()  + 0.1 * m.r()) + 1;
                const auto bottom = vp.y_to_screen(position.y() + 0.1 * m.r()) + 1;

                Ellipse(hmemDC, left, top, right, bottom);
            }

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
        printf("");
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
