#include "editor_ui.h"

#include "resource.h"
#include "simulation_xml.h"

#include <commdlg.h>
#include <wrl/client.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <map>

namespace editor_ui
{
namespace
{
    using Microsoft::WRL::ComPtr;

    enum class tool_kind { select, place, spring, damper, both };
    enum class drag_kind { none, move, marquee, velocity };

    // Properties window controls.
    enum field : int
    {
        F_M = 2001, F_R, F_E, F_X, F_Y, F_VX, F_VY, F_FIXED,
        F_SK, F_SL0, F_SMIN, F_SMAX, F_DK,
        F_XMIN, F_XMAX, F_YMIN, F_YMAX, F_DT, F_SPEED, F_MING, F_SOFT, F_WL, F_WR, F_WT, F_WB,
        F_NM, F_NR, F_NE, F_NSK, F_NDK,
        F_MCOLOUR, F_MCOLOUR_DEFAULT, F_SCOLOUR, F_SCOLOUR_DEFAULT, F_LABEL,
        F_TOOL, F_SELECTION, F_STATUS
    };

    struct state
    {
        HWND main = nullptr;
        HWND panel = nullptr;
        HINSTANCE instance = nullptr;
        editor_host host;
        editor_model model;
        bool active = false;
        bool panel_wanted = true;

        tool_kind tool = tool_kind::select;
        std::wstring path;
        std::wstring name = L"Untitled";
        std::optional<size_t> connect_first;

        struct
        {
            drag_kind kind = drag_kind::none;
            vector2 start, last;
            size_t mass = 0;
            bool gesture_started = false;
            bool additive = false;
        } drag;

        vector2 mouse_world;
        bool mouse_inside = false;

        HMENU file_menu = nullptr;
        HMENU edit_menu = nullptr;
        HMENU view_menu = nullptr;

        std::vector<int> tab_order;
        std::map<int, std::wstring> shown;
        WNDPROC edit_proc = nullptr;
    };

    state S;

    constexpr double pick_pixels = 4.0;
    constexpr int panel_client_width = 310;

    // ---- text helpers ----

    std::wstring format_number(double value)
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::wstring(buffer, result.ptr);
    }

    std::wstring to_wide(const std::string& text)
    {
        if (text.empty())
            return {};
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
        std::wstring result(size_t(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), &result[0], length);
        return result;
    }

    std::string to_utf8(const std::wstring& text)
    {
        if (text.empty())
            return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
        std::string result(size_t(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), &result[0], length, nullptr, nullptr);
        return result;
    }

    void set_clipboard_text(const std::string& utf8)
    {
        const auto wide = to_wide(utf8);
        if (!OpenClipboard(S.main))
            return;
        EmptyClipboard();
        const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            memcpy(GlobalLock(memory), wide.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        }
        CloseClipboard();
    }

    std::string get_clipboard_text()
    {
        std::string result;
        if (!OpenClipboard(S.main))
            return result;
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
        {
            if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data)))
            {
                result = to_utf8(text);
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
        return result;
    }

    // ---- properties window ----

    void set_text(int id, const std::wstring& text)
    {
        SetDlgItemTextW(S.panel, id, text.c_str());
    }

    void set_status(const std::string& message)
    {
        set_text(F_STATUS, to_wide(message));
    }

    void set_edit(int id, std::optional<double> value, bool enabled)
    {
        const std::wstring text = value ? format_number(*value) : std::wstring();
        S.shown[id] = text;
        set_text(id, text);
        EnableWindow(GetDlgItem(S.panel, id), enabled);
    }

    // Shows the shared value of `get` over `indices`, or blank when the values differ.
    template<typename Container, typename Items, typename Getter>
    void show_common(int id, const Container& indices, const Items& items, Getter get)
    {
        if (indices.empty())
        {
            set_edit(id, std::nullopt, false);
            return;
        }
        bool same = true;
        double value = 0.0;
        bool first = true;
        for (const auto i : indices)
        {
            const double v = get(items[i]);
            if (first)
            {
                value = v;
                first = false;
            }
            else if (v != value)
            {
                same = false;
            }
        }
        set_edit(id, same ? std::optional<double>(value) : std::nullopt, true);
    }

    // What the colour swatch of the current selection should show.
    enum class swatch_kind { none, mixed, standard, custom };
    struct swatch_state
    {
        swatch_kind kind = swatch_kind::none;
        uint32_t rgb = 0;
    };

    template<typename Container, typename Items>
    swatch_state colour_state(const Container& indices, const Items& items)
    {
        swatch_state result;
        bool first = true;
        for (const auto i : indices)
        {
            swatch_state one;
            one.kind = items[i].colour ? swatch_kind::custom : swatch_kind::standard;
            one.rgb = items[i].colour ? *items[i].colour : 0;
            if (first)
            {
                result = one;
                first = false;
            }
            else if (one.kind != result.kind || one.rgb != result.rgb)
            {
                result.kind = swatch_kind::mixed;
                break;
            }
        }
        return result;
    }

    COLORREF to_colorref(uint32_t rgb)
    {
        return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    }

    const wchar_t* tool_name()
    {
        switch (S.tool)
        {
        case tool_kind::place: return L"Tool: place masses (click). Esc to select";
        case tool_kind::spring: return L"Tool: spring (click two masses)";
        case tool_kind::damper: return L"Tool: damper (click two masses)";
        case tool_kind::both: return L"Tool: spring + damper (click two masses)";
        default: return L"Tool: select (M S D B to change)";
        }
    }

    void refresh_panel()
    {
        if (!S.panel)
            return;

        const auto& doc = S.model.document();
        const auto& sel = S.model.selection();

        set_text(F_TOOL, tool_name());
        set_text(F_SELECTION, L"Selected: " + std::to_wstring(sel.masses.size()) + L" masses, " + std::to_wstring(sel.springs.size()) + L" springs, " + std::to_wstring(sel.dampers.size()) + L" dampers");

        show_common(F_M, sel.masses, doc.masses, [](const editor_mass& m) { return m.m; });
        show_common(F_R, sel.masses, doc.masses, [](const editor_mass& m) { return m.r; });
        show_common(F_E, sel.masses, doc.masses, [](const editor_mass& m) { return m.e; });
        show_common(F_X, sel.masses, doc.masses, [](const editor_mass& m) { return m.position.x(); });
        show_common(F_Y, sel.masses, doc.masses, [](const editor_mass& m) { return m.position.y(); });
        show_common(F_VX, sel.masses, doc.masses, [](const editor_mass& m) { return m.velocity.x(); });
        show_common(F_VY, sel.masses, doc.masses, [](const editor_mass& m) { return m.velocity.y(); });

        int fixed_state = BST_UNCHECKED;
        if (!sel.masses.empty())
        {
            size_t fixed = 0;
            for (const auto i : sel.masses)
                fixed += doc.masses[i].fixed ? 1 : 0;
            fixed_state = fixed == sel.masses.size() ? BST_CHECKED : (fixed == 0 ? BST_UNCHECKED : BST_INDETERMINATE);
        }
        SendDlgItemMessageW(S.panel, F_FIXED, BM_SETCHECK, fixed_state, 0);
        EnableWindow(GetDlgItem(S.panel, F_FIXED), !sel.masses.empty());

        // The shared label of the selection; blank when unset (shows the mass) or when they differ.
        {
            std::wstring label_shown;
            if (!sel.masses.empty())
            {
                const auto& first = doc.masses[*sel.masses.begin()].label;
                bool same = true;
                for (const auto i : sel.masses)
                    same = same && doc.masses[i].label == first;
                if (same && first)
                    label_shown = to_wide(*first);
            }
            S.shown[F_LABEL] = label_shown;
            set_text(F_LABEL, label_shown);
            EnableWindow(GetDlgItem(S.panel, F_LABEL), !sel.masses.empty());
        }

        for (const int id : { F_MCOLOUR, F_MCOLOUR_DEFAULT })
            EnableWindow(GetDlgItem(S.panel, id), !sel.masses.empty());
        for (const int id : { F_SCOLOUR, F_SCOLOUR_DEFAULT })
            EnableWindow(GetDlgItem(S.panel, id), !sel.springs.empty());
        InvalidateRect(GetDlgItem(S.panel, F_MCOLOUR), nullptr, TRUE);
        InvalidateRect(GetDlgItem(S.panel, F_SCOLOUR), nullptr, TRUE);

        show_common(F_SK, sel.springs, doc.springs, [](const editor_spring& s) { return s.k; });
        show_common(F_SL0, sel.springs, doc.springs, [](const editor_spring& s) { return s.l0; });
        show_common(F_SMIN, sel.springs, doc.springs, [](const editor_spring& s) { return s.min_factor; });
        show_common(F_SMAX, sel.springs, doc.springs, [](const editor_spring& s) { return s.max_factor; });
        show_common(F_DK, sel.dampers, doc.dampers, [](const editor_damper& d) { return d.k; });

        set_edit(F_XMIN, doc.world.xmin, true);
        set_edit(F_XMAX, doc.world.xmax, true);
        set_edit(F_YMIN, doc.world.ymin, true);
        set_edit(F_YMAX, doc.world.ymax, true);
        set_edit(F_DT, doc.world.dt, true);
        set_edit(F_SPEED, doc.world.speed, true);
        set_edit(F_MING, doc.world.min_gravitational_mass, true);
        set_edit(F_SOFT, doc.world.softening, true);
        SendDlgItemMessageW(S.panel, F_WL, BM_SETCHECK, doc.world.bounce_left ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(S.panel, F_WR, BM_SETCHECK, doc.world.bounce_right ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(S.panel, F_WT, BM_SETCHECK, doc.world.bounce_top ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(S.panel, F_WB, BM_SETCHECK, doc.world.bounce_bottom ? BST_CHECKED : BST_UNCHECKED, 0);

        set_edit(F_NM, S.model.defaults().mass.m, true);
        set_edit(F_NR, S.model.defaults().mass.r, true);
        set_edit(F_NE, S.model.defaults().mass.e, true);
        set_edit(F_NSK, S.model.defaults().spring_k, true);
        set_edit(F_NDK, S.model.defaults().damper_k, true);
    }

    void changed()
    {
        refresh_panel();
        if (S.host.invalidate)
            S.host.invalidate();
        if (S.host.state_changed)
            S.host.state_changed();
    }

    bool apply_field(int id, double v, std::string& error)
    {
        auto world = S.model.document().world;
        switch (id)
        {
        case F_M: { mass_edit e; e.m = v; return S.model.edit_masses(e, error); }
        case F_R: { mass_edit e; e.r = v; return S.model.edit_masses(e, error); }
        case F_E: { mass_edit e; e.e = v; return S.model.edit_masses(e, error); }
        case F_X: { mass_edit e; e.x = v; return S.model.edit_masses(e, error); }
        case F_Y: { mass_edit e; e.y = v; return S.model.edit_masses(e, error); }
        case F_VX: { mass_edit e; e.vx = v; return S.model.edit_masses(e, error); }
        case F_VY: { mass_edit e; e.vy = v; return S.model.edit_masses(e, error); }
        case F_SK: { spring_edit s; s.k = v; return S.model.edit_connections(s, {}, error); }
        case F_SL0: { spring_edit s; s.l0 = v; return S.model.edit_connections(s, {}, error); }
        case F_SMIN: { spring_edit s; s.min_factor = v; return S.model.edit_connections(s, {}, error); }
        case F_SMAX: { spring_edit s; s.max_factor = v; return S.model.edit_connections(s, {}, error); }
        case F_DK: { damper_edit d; d.k = v; return S.model.edit_connections({}, d, error); }
        case F_XMIN: world.xmin = v; return S.model.edit_world(world, error);
        case F_XMAX: world.xmax = v; return S.model.edit_world(world, error);
        case F_YMIN: world.ymin = v; return S.model.edit_world(world, error);
        case F_YMAX: world.ymax = v; return S.model.edit_world(world, error);
        case F_DT: world.dt = v; return S.model.edit_world(world, error);
        case F_SPEED: world.speed = v; return S.model.edit_world(world, error);
        case F_MING: world.min_gravitational_mass = v; return S.model.edit_world(world, error);
        case F_SOFT: world.softening = v; return S.model.edit_world(world, error);
        case F_NM:
        case F_NR:
        case F_NE:
        case F_NSK:
        case F_NDK:
        {
            if (!std::isfinite(v) || (id == F_NM && !(v > 0)) || (id != F_NSK && id != F_NDK && v < 0))
            {
                error = "Value out of range";
                return false;
            }
            auto& d = S.model.defaults();
            if (id == F_NM) d.mass.m = v;
            if (id == F_NR) d.mass.r = v;
            if (id == F_NE) d.mass.e = v;
            if (id == F_NSK) d.spring_k = v;
            if (id == F_NDK) d.damper_k = v;
            return true;
        }
        }
        return true;
    }

    void commit_field(int id)
    {
        wchar_t buffer[96] = {};
        GetDlgItemTextW(S.panel, id, buffer, int(std::size(buffer)));
        const std::wstring text = buffer;

        if (id == F_LABEL)
        {
            if (text == S.shown[id])
                return;
            mass_edit e;
            if (text.empty())
                e.reset_label = true;
            else
                e.label = to_utf8(text);
            std::string error;
            S.model.edit_masses(e, error);
            set_status(error.empty() ? "Updated " + std::to_string(S.model.selection().masses.size()) + " selected mass(es)" : error);
            changed();
            return;
        }

        if (text.empty() || text == S.shown[id])
            return;

        wchar_t* end = nullptr;
        const double value = wcstod(text.c_str(), &end);
        std::string error;
        if (text.empty() || *end != L'\0' || !std::isfinite(value))
            error = "Not a number: " + to_utf8(text);
        else
            apply_field(id, value, error);

        std::string message = error;
        if (error.empty())
        {
            const auto& sel = S.model.selection();
            if (id >= F_M && id <= F_VY)
                message = "Updated " + std::to_string(sel.masses.size()) + " selected mass(es)";
            else if (id >= F_SK && id <= F_DK)
                message = "Updated the selected spring(s) / damper(s)";
            else if (id >= F_NM && id <= F_NDK)
                message = "Default set: only masses and connections added from now on use it";
            else
                message = "World updated";
        }
        set_status(message);
        changed();
    }

    void toggle_fixed()
    {
        const auto state = SendDlgItemMessageW(S.panel, F_FIXED, BM_GETCHECK, 0, 0);
        mass_edit e;
        e.fixed = state != BST_CHECKED;
        std::string error;
        S.model.edit_masses(e, error);
        set_status(error);
        changed();
        SetFocus(S.main);
    }

    void focus_neighbour(HWND from, int direction)
    {
        const int id = GetDlgCtrlID(from);
        const auto it = std::find(S.tab_order.begin(), S.tab_order.end(), id);
        if (it == S.tab_order.end())
            return;
        const int count = int(S.tab_order.size());
        for (int step = 1; step <= count; ++step)
        {
            const int next = S.tab_order[(int(it - S.tab_order.begin()) + direction * step + count * 2) % count];
            HWND control = GetDlgItem(S.panel, next);
            if (IsWindowEnabled(control))
            {
                SetFocus(control);
                SendMessageW(control, EM_SETSEL, 0, -1);
                return;
            }
        }
    }

    LRESULT CALLBACK field_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_KEYDOWN)
        {
            const int id = GetDlgCtrlID(window);
            if (wparam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                SendMessageW(window, EM_SETSEL, 0, -1);
                return 0;
            }
            if (wparam == VK_RETURN)
            {
                commit_field(id);
                SendMessageW(window, EM_SETSEL, 0, -1);
                return 0;
            }
            if (wparam == VK_ESCAPE)
            {
                refresh_panel();
                SetFocus(S.main);
                return 0;
            }
            if (wparam == VK_TAB)
            {
                commit_field(id);
                focus_neighbour(window, (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
                return 0;
            }
        }
        if (message == WM_CHAR && (wparam == VK_RETURN || wparam == VK_ESCAPE || wparam == VK_TAB))
            return 0;
        return CallWindowProcW(S.edit_proc, window, message, wparam, lparam);
    }

    void draw_swatch(const DRAWITEMSTRUCT& item)
    {
        const auto& doc = S.model.document();
        const auto& sel = S.model.selection();
        const auto state = item.CtlID == F_MCOLOUR ? colour_state(sel.masses, doc.masses) : colour_state(sel.springs, doc.springs);
        const bool enabled = IsWindowEnabled(item.hwndItem) != 0;

        COLORREF fill = GetSysColor(COLOR_BTNFACE);
        COLORREF text_colour = GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT);
        std::wstring label;
        switch (state.kind)
        {
        case swatch_kind::custom:
        {
            fill = to_colorref(state.rgb);
            const int brightness = int(GetRValue(fill)) * 3 + int(GetGValue(fill)) * 6 + int(GetBValue(fill));
            text_colour = brightness > 1280 ? RGB(0, 0, 0) : RGB(255, 255, 255);
            wchar_t hex[16];
            swprintf_s(hex, L"#%06X", state.rgb & 0xFFFFFF);
            label = hex;
            break;
        }
        case swatch_kind::standard: label = L"default"; break;
        case swatch_kind::mixed: label = L"mixed"; break;
        case swatch_kind::none: break;
        }

        HBRUSH brush = CreateSolidBrush(fill);
        FillRect(item.hDC, &item.rcItem, brush);
        DeleteObject(brush);
        FrameRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(enabled ? BLACK_BRUSH : GRAY_BRUSH)));
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, text_colour);
        RECT text_rect = item.rcItem;
        DrawTextW(item.hDC, label.c_str(), -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (item.itemState & ODS_FOCUS)
        {
            RECT focus = item.rcItem;
            InflateRect(&focus, -3, -3);
            DrawFocusRect(item.hDC, &focus);
        }
    }

    // Opens the system colour dialog (basic palette plus a full custom colour area).
    void choose_colour(bool for_masses)
    {
        const auto& doc = S.model.document();
        const auto& sel = S.model.selection();
        const auto state = for_masses ? colour_state(sel.masses, doc.masses) : colour_state(sel.springs, doc.springs);

        static COLORREF custom_colours[16] = {};
        CHOOSECOLORW dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = S.panel;
        dialog.lpCustColors = custom_colours;
        dialog.rgbResult = state.kind == swatch_kind::custom ? to_colorref(state.rgb) : (for_masses ? RGB(255, 0, 0) : RGB(0, 255, 0));
        dialog.Flags = CC_RGBINIT | CC_FULLOPEN;

        if (ChooseColorW(&dialog))
        {
            const uint32_t rgb = (uint32_t(GetRValue(dialog.rgbResult)) << 16) | (uint32_t(GetGValue(dialog.rgbResult)) << 8) | uint32_t(GetBValue(dialog.rgbResult));
            std::string error;
            if (for_masses)
            {
                mass_edit e;
                e.colour = rgb;
                S.model.edit_masses(e, error);
            }
            else
            {
                spring_edit s;
                s.colour = rgb;
                S.model.edit_connections(s, {}, error);
            }
            set_status(error);
            changed();
        }
        SetFocus(S.main);
    }

    void reset_colour(bool for_masses)
    {
        std::string error;
        if (for_masses)
        {
            mass_edit e;
            e.reset_colour = true;
            S.model.edit_masses(e, error);
        }
        else
        {
            spring_edit s;
            s.reset_colour = true;
            S.model.edit_connections(s, {}, error);
        }
        set_status(error);
        changed();
        SetFocus(S.main);
    }

    void toggle_wall(int id)
    {
        const bool on = SendDlgItemMessageW(S.panel, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
        auto world = S.model.document().world;
        if (id == F_WL) world.bounce_left = on;
        if (id == F_WR) world.bounce_right = on;
        if (id == F_WT) world.bounce_top = on;
        if (id == F_WB) world.bounce_bottom = on;

        std::string error;
        S.model.edit_world(world, error);
        set_status(error);
        changed();
        SetFocus(S.main);
    }

    LRESULT CALLBACK panel_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        switch (message)
        {
        case WM_COMMAND:
            if (HIWORD(wparam) == EN_KILLFOCUS)
                commit_field(LOWORD(wparam));
            else if (LOWORD(wparam) == F_FIXED && HIWORD(wparam) == BN_CLICKED)
                toggle_fixed();
            else if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) >= F_WL && LOWORD(wparam) <= F_WB)
                toggle_wall(LOWORD(wparam));
            else if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == F_MCOLOUR)
                choose_colour(true);
            else if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == F_SCOLOUR)
                choose_colour(false);
            else if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == F_MCOLOUR_DEFAULT)
                reset_colour(true);
            else if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == F_SCOLOUR_DEFAULT)
                reset_colour(false);
            return 0;
        case WM_DRAWITEM:
            draw_swatch(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
            return TRUE;
        case WM_CLOSE:
            S.panel_wanted = false;
            ShowWindow(window, SW_HIDE);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    HWND add_static(const wchar_t* text, int x, int y, int w, int h, int id = -1)
    {
        HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, S.panel, id < 0 ? nullptr : reinterpret_cast<HMENU>(INT_PTR(id)), S.instance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return control;
    }

    void create_panel()
    {
        WNDCLASSEXW cls = {};
        cls.cbSize = sizeof(cls);
        cls.lpfnWndProc = panel_proc;
        cls.hInstance = S.instance;
        cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        cls.lpszClassName = L"PhysicsEditorPanel";
        RegisterClassExW(&cls);

        // The window is resized to fit once all controls are placed.
        const int row = 24;
        RECT rc = { 0, 0, panel_client_width, 100 };
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        AdjustWindowRectEx(&rc, style, FALSE, WS_EX_TOOLWINDOW);

        S.panel = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"Properties", style, 0, 0, rc.right - rc.left, rc.bottom - rc.top, S.main, nullptr, S.instance, nullptr);

        int y = 8;
        add_static(L"", 8, y, panel_client_width - 16, 18, F_TOOL);
        y += 22;
        add_static(L"", 8, y, panel_client_width - 16, 18, F_SELECTION);
        y += 22;

        auto header = [&](const wchar_t* title) {
            add_static(title, 8, y + 4, panel_client_width - 16, 18);
            y += row + 4;
        };
        auto edit_row = [&](int id, const wchar_t* label) {
            add_static(label, 8, y + 3, 130, 18);
            HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 140, y, 150, row - 2, S.panel, reinterpret_cast<HMENU>(INT_PTR(id)), S.instance, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            S.edit_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(control, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(field_proc)));
            S.tab_order.push_back(id);
            y += row;
        };

        header(L"Selected masses");
        edit_row(F_M, L"Mass (kg)");
        edit_row(F_R, L"Radius");
        edit_row(F_E, L"Elasticity");
        edit_row(F_X, L"x");
        edit_row(F_Y, L"y");
        edit_row(F_VX, L"Velocity x");
        edit_row(F_VY, L"Velocity y");
        HWND fixed = CreateWindowExW(0, L"BUTTON", L"Fixed in place", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_3STATE, 140, y, 150, row - 2, S.panel, reinterpret_cast<HMENU>(INT_PTR(F_FIXED)), S.instance, nullptr);
        SendMessageW(fixed, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        y += row;

        // A swatch showing the shared colour of the selection, which opens the colour dialog, and a reset button.
        auto colour_row = [&](int swatch_id, int default_id) {
            add_static(L"Colour", 8, y + 3, 130, 18);
            HWND swatch = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 140, y, 104, row - 2, S.panel, reinterpret_cast<HMENU>(INT_PTR(swatch_id)), S.instance, nullptr);
            HWND reset = CreateWindowExW(0, L"BUTTON", L"Default", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 248, y, 44, row - 2, S.panel, reinterpret_cast<HMENU>(INT_PTR(default_id)), S.instance, nullptr);
            SendMessageW(swatch, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            SendMessageW(reset, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            y += row;
        };
        colour_row(F_MCOLOUR, F_MCOLOUR_DEFAULT);
        edit_row(F_LABEL, L"Label (blank = the mass)");

        header(L"Selected spring / damper");
        edit_row(F_SK, L"Spring k");
        edit_row(F_SL0, L"Rest length");
        edit_row(F_SMIN, L"Break below (x length)");
        edit_row(F_SMAX, L"Break above (x length)");
        colour_row(F_SCOLOUR, F_SCOLOUR_DEFAULT);
        edit_row(F_DK, L"Damper k");

        header(L"World");
        edit_row(F_XMIN, L"x min (left edge)");
        edit_row(F_XMAX, L"x max (right edge)");
        edit_row(F_YMIN, L"y min (top edge)");
        edit_row(F_YMAX, L"y max (bottom edge)");
        edit_row(F_DT, L"Time step");
        edit_row(F_SPEED, L"Start speed (x real time)");
        edit_row(F_MING, L"Gravity min mass");
        edit_row(F_SOFT, L"Gravity softening (0 = none)");

        add_static(L"Bounce off these edges:", 8, y + 3, panel_client_width - 16, 18);
        y += row;
        const struct { int id; const wchar_t* label; } walls[] = { { F_WL, L"Left" }, { F_WR, L"Right" }, { F_WT, L"Top" }, { F_WB, L"Bottom" } };
        int wall_x = 8;
        for (const auto& wall : walls)
        {
            HWND box = CreateWindowExW(0, L"BUTTON", wall.label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, wall_x, y, 70, row - 2, S.panel, reinterpret_cast<HMENU>(INT_PTR(wall.id)), S.instance, nullptr);
            SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            wall_x += 74;
        }
        y += row;

        header(L"Defaults for newly placed items");
        edit_row(F_NM, L"New mass (kg)");
        edit_row(F_NR, L"New radius");
        edit_row(F_NE, L"New elasticity");
        edit_row(F_NSK, L"New spring k");
        edit_row(F_NDK, L"New damper k");

        add_static(L"", 8, y + 4, panel_client_width - 16, 18, F_STATUS);

        RECT fitted = { 0, 0, panel_client_width, y + 4 + 18 + 8 };
        AdjustWindowRectEx(&fitted, style, FALSE, WS_EX_TOOLWINDOW);
        SetWindowPos(S.panel, nullptr, 0, 0, fitted.right - fitted.left, fitted.bottom - fitted.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // ---- files ----

    bool pick_file(bool save, std::wstring& path)
    {
        wchar_t buffer[MAX_PATH] = {};
        wcsncpy_s(buffer, path.c_str(), _TRUNCATE);
        const std::wstring directory = models_directory();

        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = S.main;
        ofn.lpstrFilter = L"Simulation XML (*.xml)\0*.xml\0All files (*.*)\0*.*\0";
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"xml";
        ofn.lpstrInitialDir = directory.c_str();
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

        if (!(save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn)))
            return false;
        path = buffer;
        return true;
    }

    void show_error(const std::string& message, const char* title)
    {
        MessageBoxA(S.main, message.c_str(), title, MB_OK | MB_ICONERROR);
    }

    bool save_document(bool save_as)
    {
        std::wstring target = S.path;
        if (save_as || target.empty())
        {
            if (target.empty())
                target = S.name + L".xml";
            if (!pick_file(true, target))
                return false;
        }

        std::vector<mass_state> ms;
        std::vector<spring_state> ss;
        std::vector<damper_state> ds;
        const auto sim = build_simulation(S.model.document(), ms, ss, ds);

        std::string error;
        if (!save_simulation_xml(target, *sim, ms, error))
        {
            show_error(error, "Cannot save simulation");
            return false;
        }

        S.path = target;
        S.model.mark_clean();
        changed();
        return true;
    }

    void adopt_document(editor_document doc, const std::wstring& path, const std::wstring& name)
    {
        S.model.reset(std::move(doc));
        S.path = path;
        S.name = name;
        S.tool = tool_kind::select;
        S.connect_first.reset();
        S.drag.kind = drag_kind::none;
        set_status("");
        if (S.host.enter_edit_mode)
            S.host.enter_edit_mode();
        changed();
    }

    bool open_file(const std::wstring& path, const std::wstring& name, bool keep_path)
    {
        std::vector<mass_state> ms;
        std::vector<spring_state> ss;
        std::vector<damper_state> ds;
        std::string error;
        const auto sim = load_simulation_xml(path, ms, ss, ds, error);
        if (!sim)
        {
            show_error(error, "Cannot open simulation");
            return false;
        }
        adopt_document(document_from_simulation(*sim, ms), keep_path ? path : std::wstring(), name);
        return true;
    }

    // ---- editing commands ----

    vector2 paste_anchor()
    {
        const auto view = S.host.view();
        POINT cursor;
        GetCursorPos(&cursor);
        ScreenToClient(S.main, &cursor);
        const bool inside = cursor.x >= view.canvas.left && cursor.x < view.canvas.right && cursor.y >= view.canvas.top && cursor.y < view.canvas.bottom;
        return inside ? view.to_world(cursor.x, cursor.y) : view.to_world(view.width() / 2, view.height() / 2);
    }

    void do_paste()
    {
        const auto text = get_clipboard_text();
        std::string error;
        if (text.empty() || !S.model.paste_at(text, paste_anchor(), error))
            set_status(text.empty() ? "Nothing to paste" : "Cannot paste: " + error);
        else
            set_status("");
        changed();
    }

    void set_tool(tool_kind tool)
    {
        S.tool = tool;
        S.connect_first.reset();
        changed();
    }

    // ---- hit testing helpers ----

    // Velocity arrows show where a mass would be after one real second at the starting speed.
    double arrow_scale()
    {
        return S.model.document().world.speed;
    }

    std::optional<size_t> velocity_handle_at(const editor_view& view, vector2 point)
    {
        const double tolerance = 6.0 / view.pixels_per_unit();
        for (const auto i : S.model.selection().masses)
        {
            const auto& m = S.model.document().masses[i];
            if (m.velocity.x() == 0.0 && m.velocity.y() == 0.0)
                continue;
            if ((m.position + m.velocity * arrow_scale()).distance(point) <= tolerance)
                return i;
        }
        return std::nullopt;
    }

    select_mode mode_for(WPARAM keys)
    {
        return (keys & MK_CONTROL) ? select_mode::toggle : select_mode::replace;
    }

    void begin_drag(drag_kind kind, vector2 start)
    {
        S.drag.kind = kind;
        S.drag.start = start;
        S.drag.last = start;
        S.drag.gesture_started = false;
        SetCapture(S.main);
    }

    void ensure_gesture()
    {
        if (!S.drag.gesture_started)
        {
            S.model.begin_gesture();
            S.drag.gesture_started = true;
        }
    }
}   // namespace

// ---- public interface ----

std::wstring models_directory()
{
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring directory = exe_path;
    directory.resize(directory.find_last_of(L'\\') + 1);
    return directory + L"models";
}

editor_model& model()
{
    return S.model;
}

bool active()
{
    return S.active;
}

void set_active(bool active)
{
    S.active = active;
    if (S.panel)
    {
        if (active && S.panel_wanted)
        {
            if (!IsWindowVisible(S.panel))
            {
                RECT rc;
                GetWindowRect(S.main, &rc);
                RECT panel_rc;
                GetWindowRect(S.panel, &panel_rc);
                SetWindowPos(S.panel, nullptr, rc.right - (panel_rc.right - panel_rc.left) - 24, rc.top + 90, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            ShowWindow(S.panel, SW_SHOWNOACTIVATE);
        }
        else
        {
            ShowWindow(S.panel, SW_HIDE);
        }
    }
    if (active)
        refresh_panel();
    update_menus();
}

std::wstring document_title()
{
    std::wstring title = S.name;
    if (!S.path.empty())
    {
        const auto slash = S.path.find_last_of(L'\\');
        title = slash == std::wstring::npos ? S.path : S.path.substr(slash + 1);
    }
    return S.model.dirty() ? title + L"*" : title;
}

bool confirm_discard()
{
    if (!S.model.dirty())
        return true;

    const auto message = L"Save changes to " + document_title() + L"?";
    switch (MessageBoxW(S.main, message.c_str(), L"Simulation editor", MB_YESNOCANCEL | MB_ICONQUESTION))
    {
    case IDYES: return save_document(false);
    case IDNO: return true;
    default: return false;
    }
}

bool build_run(std::shared_ptr<simulation>& sim, std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers)
{
    if (S.model.document().masses.empty())
    {
        MessageBoxW(S.main, L"Place at least one mass before running.", L"Simulation editor", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    sim = build_simulation(S.model.document(), masses, springs, dampers);
    return true;
}

bool command(int id)
{
    if (id >= ID_EDITOR_BUNDLED_FIRST && id <= ID_EDITOR_BUNDLED_LAST)
    {
        if (!confirm_discard())
            return true;
        const int number = id - ID_EDITOR_BUNDLED_FIRST + 1;
        open_file(models_directory() + L"\\simulation" + std::to_wstring(number) + L".xml", L"simulation" + std::to_wstring(number) + L" (copy)", false);
        return true;
    }

    switch (id)
    {
    case ID_EDITOR_BUNDLED_SOLAR:
        if (confirm_discard())
            open_file(models_directory() + L"\\solar_system.xml", L"solar_system (copy)", false);
        return true;
    case ID_EDITOR_BUNDLED_MILKYWAY:
        if (confirm_discard())
            open_file(models_directory() + L"\\milky_way.xml", L"milky_way (copy)", false);
        return true;
    case ID_EDITOR_NEW:
        if (confirm_discard())
            adopt_document(editor_document(), std::wstring(), L"Untitled");
        return true;
    case ID_EDITOR_OPEN:
    {
        if (!confirm_discard())
            return true;
        std::wstring path;   // the dialog starts in the models folder; a directory in the file name box can make it fail to open
        if (pick_file(false, path))
            open_file(path, L"", true);
        return true;
    }
    case ID_EDITOR_SAVE:
        save_document(false);
        return true;
    case ID_EDITOR_SAVEAS:
        save_document(true);
        return true;
    case ID_EDITOR_PROPERTIES:
        S.panel_wanted = !S.panel_wanted;
        set_active(S.active);
        return true;
    case ID_EDIT_UNDO:
        S.model.undo();
        changed();
        return true;
    case ID_EDIT_REDO:
        S.model.redo();
        changed();
        return true;
    case ID_EDIT_CUT:
    {
        const auto text = S.model.cut_selection();
        if (!text.empty())
            set_clipboard_text(text);
        changed();
        return true;
    }
    case ID_EDIT_COPY:
    {
        const auto text = S.model.copy_selection();
        if (!text.empty())
            set_clipboard_text(text);
        return true;
    }
    case ID_EDIT_PASTE:
        do_paste();
        return true;
    case ID_EDIT_DUPLICATE:
        S.model.duplicate_selection();
        changed();
        return true;
    case ID_EDIT_DELETE:
        S.model.delete_selection();
        changed();
        return true;
    case ID_EDIT_SELECTALL:
        S.model.select_all();
        changed();
        return true;
    case ID_TOOL_SELECT: set_tool(tool_kind::select); return true;
    case ID_TOOL_PLACE: set_tool(tool_kind::place); return true;
    case ID_TOOL_SPRING: set_tool(tool_kind::spring); return true;
    case ID_TOOL_DAMPER: set_tool(tool_kind::damper); return true;
    case ID_TOOL_BOTH: set_tool(tool_kind::both); return true;
    }
    return false;
}

void update_menus()
{
    if (!S.edit_menu)
        return;

    auto enable = [](HMENU menu, int id, bool on) {
        EnableMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
    };

    const bool editing = S.active;
    const bool has_selection = !S.model.selection().empty();
    enable(S.edit_menu, ID_EDIT_UNDO, editing && S.model.can_undo());
    enable(S.edit_menu, ID_EDIT_REDO, editing && S.model.can_redo());
    enable(S.edit_menu, ID_EDIT_CUT, editing && !S.model.selection().masses.empty());
    enable(S.edit_menu, ID_EDIT_COPY, editing && !S.model.selection().masses.empty());
    enable(S.edit_menu, ID_EDIT_PASTE, editing && IsClipboardFormatAvailable(CF_UNICODETEXT));
    enable(S.edit_menu, ID_EDIT_DUPLICATE, editing && !S.model.selection().masses.empty());
    enable(S.edit_menu, ID_EDIT_DELETE, editing && has_selection);
    enable(S.edit_menu, ID_EDIT_SELECTALL, editing);
    for (const int id : { ID_TOOL_SELECT, ID_TOOL_PLACE, ID_TOOL_SPRING, ID_TOOL_DAMPER, ID_TOOL_BOTH })
        enable(S.edit_menu, id, editing);

    const int tool_ids[] = { ID_TOOL_SELECT, ID_TOOL_PLACE, ID_TOOL_SPRING, ID_TOOL_DAMPER, ID_TOOL_BOTH };
    CheckMenuRadioItem(S.edit_menu, ID_TOOL_SELECT, ID_TOOL_BOTH, tool_ids[int(S.tool)], MF_BYCOMMAND);
    CheckMenuItem(S.view_menu, ID_EDITOR_PROPERTIES, MF_BYCOMMAND | (S.panel_wanted ? MF_CHECKED : MF_UNCHECKED));
}

void create(HWND main_window, HINSTANCE instance, editor_host host)
{
    S.main = main_window;
    S.instance = instance;
    S.host = std::move(host);

    HMENU bar = GetMenu(main_window);

    // Bundled model names come from the existing Simulation menu.
    HMENU bundled = CreatePopupMenu();
    for (int n = 1; n <= 9; ++n)
    {
        wchar_t text[64] = {};
        if (GetMenuStringW(bar, ID_SIMULATION1 + n - 1, text, int(std::size(text)), MF_BYCOMMAND) == 0)
            swprintf_s(text, L"Simulation %d", n);
        AppendMenuW(bundled, MF_STRING, ID_EDITOR_BUNDLED_FIRST + n - 1, text);
    }
    AppendMenuW(bundled, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(bundled, MF_STRING, ID_EDITOR_BUNDLED_SOLAR, L"Solar system (10 Oct 2026)");
    AppendMenuW(bundled, MF_STRING, ID_EDITOR_BUNDLED_MILKYWAY, L"Milky way (50000 stars)");

    S.file_menu = GetSubMenu(bar, 0);
    InsertMenuW(S.file_menu, 0, MF_BYPOSITION | MF_STRING, ID_EDITOR_NEW, L"&New\tCtrl+N");
    InsertMenuW(S.file_menu, 1, MF_BYPOSITION | MF_STRING, ID_EDITOR_OPEN, L"&Open...\tCtrl+O");
    InsertMenuW(S.file_menu, 2, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(bundled), L"Open &bundled simulation");
    InsertMenuW(S.file_menu, 3, MF_BYPOSITION | MF_STRING, ID_EDITOR_SAVE, L"&Save\tCtrl+S");
    InsertMenuW(S.file_menu, 4, MF_BYPOSITION | MF_STRING, ID_EDITOR_SAVEAS, L"Save &As...\tCtrl+Shift+S");
    InsertMenuW(S.file_menu, 5, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

    HMENU simulation_menu = GetSubMenu(bar, 1);
    InsertMenuW(simulation_menu, 0, MF_BYPOSITION | MF_STRING, ID_EDITOR_RUN, L"&Run / back to editor\tF5");
    InsertMenuW(simulation_menu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

    S.edit_menu = CreatePopupMenu();
    AppendMenuW(S.edit_menu, MF_STRING, ID_EDIT_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(S.edit_menu, MF_STRING, ID_EDIT_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(S.edit_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(S.edit_menu, MF_STRING, ID_EDIT_CUT, L"Cu&t\tCtrl+X");
    AppendMenuW(S.edit_menu, MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
    AppendMenuW(S.edit_menu, MF_STRING, ID_EDIT_PASTE, L"&Paste\tCtrl+V");
    AppendMenuW(S.edit_menu, MF_STRING, ID_EDIT_DUPLICATE, L"D&uplicate\tCtrl+D");
    AppendMenuW(S.edit_menu, MF_STRING, ID_EDIT_DELETE, L"&Delete\tDel");
    AppendMenuW(S.edit_menu, MF_STRING, ID_EDIT_SELECTALL, L"Select &all\tCtrl+A");
    AppendMenuW(S.edit_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(S.edit_menu, MF_STRING, ID_TOOL_SELECT, L"Tool: &select\tV");
    AppendMenuW(S.edit_menu, MF_STRING, ID_TOOL_PLACE, L"Tool: place &mass\tM");
    AppendMenuW(S.edit_menu, MF_STRING, ID_TOOL_SPRING, L"Tool: s&pring\tS");
    AppendMenuW(S.edit_menu, MF_STRING, ID_TOOL_DAMPER, L"Tool: d&amper\tD");
    AppendMenuW(S.edit_menu, MF_STRING, ID_TOOL_BOTH, L"Tool: spring + &damper\tB");
    InsertMenuW(bar, 1, MF_BYPOSITION | MF_POPUP, reinterpret_cast<UINT_PTR>(S.edit_menu), L"&Edit");

    S.view_menu = GetSubMenu(bar, 3);
    AppendMenuW(S.view_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(S.view_menu, MF_STRING, ID_EDITOR_PROPERTIES, L"&Properties window");
    DrawMenuBar(main_window);

    create_panel();
    refresh_panel();
    update_menus();
}

void destroy()
{
    if (S.panel)
    {
        DestroyWindow(S.panel);
        S.panel = nullptr;
    }
}

void on_mouse_down(int x, int y, WPARAM keys)
{
    SetFocus(S.main);   // commits any field being edited before the click is interpreted

    const auto view = S.host.view();
    const auto point = view.to_world(x, y);
    const double margin = pick_pixels / view.pixels_per_unit();
    S.mouse_world = point;

    switch (S.tool)
    {
    case tool_kind::place:
        S.model.add_mass(point);
        changed();
        return;

    case tool_kind::spring:
    case tool_kind::damper:
    case tool_kind::both:
    {
        const auto hit = S.model.pick_mass(point, margin);
        if (!hit)
        {
            S.connect_first.reset();
        }
        else if (!S.connect_first)
        {
            S.connect_first = hit;
        }
        else
        {
            const auto kind = S.tool == tool_kind::spring ? connection_kind::spring : (S.tool == tool_kind::damper ? connection_kind::damper : connection_kind::both);
            S.model.connect(*S.connect_first, *hit, kind);
            S.connect_first.reset();
        }
        changed();
        return;
    }

    case tool_kind::select:
        break;
    }

    if (const auto handle = velocity_handle_at(view, point))
    {
        S.drag.mass = *handle;
        begin_drag(drag_kind::velocity, point);
        return;
    }

    if (const auto mass = S.model.pick_mass(point, margin))
    {
        if (keys & MK_SHIFT)
        {
            // Shift+drag from a mass sets its velocity.
            S.model.select_mass(*mass, select_mode::replace);
            S.drag.mass = *mass;
            begin_drag(drag_kind::velocity, point);
            changed();
            return;
        }
        if (keys & MK_CONTROL)
        {
            S.model.select_mass(*mass, select_mode::toggle);
            changed();
            return;
        }
        if (!S.model.selection().masses.count(*mass))
            S.model.select_mass(*mass, select_mode::replace);
        begin_drag(drag_kind::move, point);
        changed();
        return;
    }

    if (const auto connection = S.model.pick_connection(point, margin))
    {
        S.model.select_connection(*connection, mode_for(keys));
        changed();
        return;
    }

    S.drag.additive = (keys & MK_CONTROL) != 0;
    if (!S.drag.additive)
        S.model.clear_selection();
    begin_drag(drag_kind::marquee, point);
    changed();
}

void on_mouse_move(int x, int y, WPARAM)
{
    const auto view = S.host.view();
    const auto point = view.to_world(x, y);
    S.mouse_world = point;
    S.mouse_inside = true;

    switch (S.drag.kind)
    {
    case drag_kind::move:
    {
        const auto delta = point - S.drag.last;
        if (delta.x() != 0.0 || delta.y() != 0.0)
        {
            ensure_gesture();
            S.model.move_selection(delta);
            S.drag.last = point;
            refresh_panel();
            S.host.invalidate();
        }
        break;
    }
    case drag_kind::velocity:
    {
        ensure_gesture();
        const auto& m = S.model.document().masses[S.drag.mass];
        S.model.set_velocity(S.drag.mass, (point - m.position) / arrow_scale());
        refresh_panel();
        S.host.invalidate();
        break;
    }
    case drag_kind::marquee:
        S.drag.last = point;
        S.host.invalidate();
        break;
    case drag_kind::none:
        if (S.tool != tool_kind::select || S.connect_first)
            S.host.invalidate();
        break;
    }
}

void on_mouse_up(int, int, WPARAM)
{
    if (S.drag.kind == drag_kind::none)
        return;

    ReleaseCapture();
    if (S.drag.kind == drag_kind::marquee)
    {
        const auto inside = S.model.masses_in_rect(S.drag.start, S.drag.last);
        S.model.select_masses(inside, S.drag.additive ? select_mode::add : select_mode::replace);
    }
    S.drag.kind = drag_kind::none;
    changed();
}

void on_context_menu(int screen_x, int screen_y)
{
    POINT client = { screen_x, screen_y };
    ScreenToClient(S.main, &client);
    const auto view = S.host.view();
    const auto point = view.to_world(client.x, client.y);

    if (const auto mass = S.model.pick_mass(point, pick_pixels / view.pixels_per_unit()))
    {
        if (!S.model.selection().masses.count(*mass))
        {
            S.model.select_mass(*mass, select_mode::replace);
            changed();
        }
    }
    S.mouse_world = point;

    update_menus();
    HMENU popup = CreatePopupMenu();
    AppendMenuW(popup, MF_STRING, ID_EDIT_CUT, L"Cu&t");
    AppendMenuW(popup, MF_STRING, ID_EDIT_COPY, L"&Copy");
    AppendMenuW(popup, MF_STRING, ID_EDIT_PASTE, L"&Paste");
    AppendMenuW(popup, MF_STRING, ID_EDIT_DUPLICATE, L"D&uplicate");
    AppendMenuW(popup, MF_STRING, ID_EDIT_DELETE, L"&Delete");
    const bool masses = !S.model.selection().masses.empty();
    EnableMenuItem(popup, ID_EDIT_CUT, MF_BYCOMMAND | (masses ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(popup, ID_EDIT_COPY, MF_BYCOMMAND | (masses ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(popup, ID_EDIT_DUPLICATE, MF_BYCOMMAND | (masses ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(popup, ID_EDIT_DELETE, MF_BYCOMMAND | (!S.model.selection().empty() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(popup, ID_EDIT_PASTE, MF_BYCOMMAND | (IsClipboardFormatAvailable(CF_UNICODETEXT) ? MF_ENABLED : MF_GRAYED));
    TrackPopupMenu(popup, TPM_RIGHTBUTTON, screen_x, screen_y, 0, S.main, nullptr);
    DestroyMenu(popup);
}

bool on_key_down(WPARAM key)
{
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (ctrl)
    {
        switch (key)
        {
        case 'C': return command(ID_EDIT_COPY);
        case 'X': return command(ID_EDIT_CUT);
        case 'V': return command(ID_EDIT_PASTE);
        case 'Z': return command(shift ? ID_EDIT_REDO : ID_EDIT_UNDO);
        case 'Y': return command(ID_EDIT_REDO);
        case 'A': return command(ID_EDIT_SELECTALL);
        case 'D': return command(ID_EDIT_DUPLICATE);
        case 'N': return command(ID_EDITOR_NEW);
        case 'O': return command(ID_EDITOR_OPEN);
        case 'S': return command(shift ? ID_EDITOR_SAVEAS : ID_EDITOR_SAVE);
        }
        return false;
    }

    switch (key)
    {
    case VK_DELETE: return command(ID_EDIT_DELETE);
    case VK_ESCAPE: return command(ID_TOOL_SELECT);
    case 'V': return command(ID_TOOL_SELECT);
    case 'M': return command(ID_TOOL_PLACE);
    case 'S': return command(ID_TOOL_SPRING);
    case 'D': return command(ID_TOOL_DAMPER);
    case 'B': return command(ID_TOOL_BOTH);
    }
    return false;
}

void paint(ID2D1RenderTarget* target)
{
    const auto view = S.host.view();
    const auto& doc = S.model.document();
    const auto& sel = S.model.selection();
    const float px = float(view.pixels_per_unit());

    auto make_brush = [&](float r, float g, float b) {
        ComPtr<ID2D1SolidColorBrush> brush;
        target->CreateSolidColorBrush(D2D1::ColorF(r, g, b), &brush);
        return brush;
    };
    const auto green = make_brush(0.0f, 1.0f, 0.0f);
    const auto red = make_brush(1.0f, 0.0f, 0.0f);
    const auto blue = make_brush(0.2f, 0.3f, 1.0f);
    const auto yellow = make_brush(1.0f, 0.9f, 0.1f);
    const auto white = make_brush(1.0f, 1.0f, 1.0f);
    const auto grey = make_brush(0.35f, 0.35f, 0.35f);
    const auto cyan = make_brush(0.2f, 0.9f, 0.9f);
    const auto orange = make_brush(1.0f, 0.5f, 0.1f);
    const auto magenta = make_brush(1.0f, 0.2f, 1.0f);
    const auto custom = make_brush(1.0f, 1.0f, 1.0f);   // recoloured per object
    auto custom_brush = [&](uint32_t rgb) -> ID2D1Brush* {
        custom->SetColor(D2D1::ColorF(rgb));
        return custom.Get();
    };

    // World bounds; edges that masses bounce off are drawn thick and orange.
    const auto top_left = view.to_screen(vector2(doc.world.xmin, doc.world.ymin));
    const auto bottom_right = view.to_screen(vector2(doc.world.xmax, doc.world.ymax));
    auto draw_wall = [&](D2D1_POINT_2F a, D2D1_POINT_2F b, bool bounces) {
        target->DrawLine(a, b, bounces ? orange.Get() : grey.Get(), bounces ? 2.5f : 1.0f);
    };
    draw_wall(top_left, D2D1::Point2F(bottom_right.x, top_left.y), doc.world.bounce_top);
    draw_wall(D2D1::Point2F(top_left.x, bottom_right.y), bottom_right, doc.world.bounce_bottom);
    draw_wall(top_left, D2D1::Point2F(top_left.x, bottom_right.y), doc.world.bounce_left);
    draw_wall(D2D1::Point2F(bottom_right.x, top_left.y), bottom_right, doc.world.bounce_right);

    for (size_t i = 0; i < doc.springs.size(); ++i)
    {
        const auto& s = doc.springs[i];
        const bool selected = sel.springs.count(i) > 0;
        ID2D1Brush* line_brush = selected ? yellow.Get() : (s.colour ? custom_brush(*s.colour) : green.Get());
        target->DrawLine(view.to_screen(doc.masses[s.a].position), view.to_screen(doc.masses[s.b].position), line_brush, selected ? 2.5f : 1.0f);
    }

    for (size_t i = 0; i < doc.dampers.size(); ++i)
    {
        const auto& d = doc.dampers[i];
        auto a = view.to_screen(doc.masses[d.a].position);
        auto b = view.to_screen(doc.masses[d.b].position);
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 0.0f)
        {
            // Offset sideways so a damper beside a spring on the same pair stays visible.
            const float ox = -dy / length * 3.0f;
            const float oy = dx / length * 3.0f;
            a = D2D1::Point2F(a.x + ox, a.y + oy);
            b = D2D1::Point2F(b.x + ox, b.y + oy);
        }
        const bool selected = sel.dampers.count(i) > 0;
        target->DrawLine(a, b, selected ? yellow.Get() : cyan.Get(), selected ? 2.0f : 1.0f);
    }

    for (size_t i = 0; i < doc.masses.size(); ++i)
    {
        const auto& m = doc.masses[i];
        const auto centre = view.to_screen(m.position);
        if (centre.x < -50 || centre.y < -50 || centre.x > float(view.width()) + 50 || centre.y > float(view.height()) + 50)
            continue;

        const float radius = (std::max)(float(m.r) * px, 3.0f);
        const auto ellipse = D2D1::Ellipse(centre, radius, radius);
        const bool selected = sel.masses.count(i) > 0;
        ID2D1Brush* fill_brush = m.fixed ? blue.Get() : red.Get();
        ID2D1Brush* outline_brush = green.Get();
        if (m.colour)
        {
            fill_brush = custom_brush(*m.colour);
            outline_brush = fill_brush;
        }
        target->FillEllipse(ellipse, fill_brush);
        target->DrawEllipse(ellipse, selected ? yellow.Get() : outline_brush, selected ? 2.5f : 1.0f);

        if (selected && (m.velocity.x() != 0.0 || m.velocity.y() != 0.0))
        {
            const auto tip = view.to_screen(m.position + m.velocity * arrow_scale());
            target->DrawLine(centre, tip, white.Get(), 1.5f);
            target->DrawEllipse(D2D1::Ellipse(tip, 4.0f, 4.0f), white.Get(), 1.5f);
        }
    }

    if (S.connect_first && *S.connect_first < doc.masses.size())
    {
        target->DrawLine(view.to_screen(doc.masses[*S.connect_first].position), view.to_screen(S.mouse_world), magenta.Get(), 1.5f);
    }
    else if (S.tool == tool_kind::place && S.mouse_inside)
    {
        const float radius = (std::max)(float(S.model.defaults().mass.r) * px, 3.0f);
        target->DrawEllipse(D2D1::Ellipse(view.to_screen(S.mouse_world), radius, radius), white.Get(), 1.0f);
    }

    if (S.drag.kind == drag_kind::marquee)
    {
        const auto a = view.to_screen(S.drag.start);
        const auto b = view.to_screen(S.drag.last);
        target->DrawRectangle(D2D1::RectF((std::min)(a.x, b.x), (std::min)(a.y, b.y), (std::max)(a.x, b.x), (std::max)(a.y, b.y)), white.Get(), 1.0f);
    }
}

}   // namespace editor_ui
