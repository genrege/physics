#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "editor_model.h"

#include <windows.h>
#include <d2d1.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Maps the world onto the canvas (the whole client area), matching the run-mode viewport.
struct editor_view
{
    RECT canvas{};
    double xmin = 0.0, xmax = 1.0, ymin = 0.0, ymax = 1.0;   // world bounds shown in the canvas

    double width() const { return double(canvas.right - canvas.left); }
    double height() const { return double(canvas.bottom - canvas.top); }
    double pixels_per_unit() const { return width() / (xmax - xmin); }

    vector2 to_world(double px, double py) const
    {
        return vector2(xmin + px * (xmax - xmin) / width(), ymin + py * (ymax - ymin) / height());
    }

    D2D1_POINT_2F to_screen(vector2 w) const
    {
        return D2D1::Point2F(float((w.x() - xmin) * width() / (xmax - xmin)), float((w.y() - ymin) * height() / (ymax - ymin)));
    }
};

// Services the editor needs from the application window.
struct editor_host
{
    std::function<void()> invalidate;           // repaint the canvas
    std::function<editor_view()> view;          // current canvas mapping
    std::function<void()> enter_edit_mode;      // stop the simulation and show the editor
    std::function<void()> state_changed;        // document name or dirty state changed (title bar)
};

namespace editor_ui
{
    // Adds the Editor and Edit menus and creates the (initially hidden) properties window.
    void create(HWND main_window, HINSTANCE instance, editor_host host);
    void destroy();

    editor_model& model();
    std::wstring models_directory();

    void set_active(bool active);
    bool active();

    // "name" or "name*" while the document has unsaved changes.
    std::wstring document_title();

    // Asks to save unsaved changes; false means the user cancelled.
    bool confirm_discard();

    // Builds the simulation to run from the document; false (after telling the user) if there is nothing to run.
    bool build_run(std::shared_ptr<simulation>& sim, std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers);

    // Handles Editor/Edit/tool menu commands; Run is left to the application. False if `id` is not ours.
    bool command(int id);
    void update_menus();

    void on_mouse_down(int x, int y, WPARAM keys);
    void on_mouse_move(int x, int y, WPARAM keys);
    void on_mouse_up(int x, int y, WPARAM keys);
    void on_context_menu(int screen_x, int screen_y);
    bool on_key_down(WPARAM virtual_key);

    // Draws the document; call between BeginDraw and EndDraw.
    void paint(ID2D1RenderTarget* target);
}
