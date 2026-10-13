#pragma once

#include "mass.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Label text and placement. No Windows code, so it is unit tested.

// "1.99e30 kg"
inline std::string format_mass(double kg)
{
    char text[64];
    snprintf(text, sizeof(text), "%.3g", kg);
    std::string s = text;

    const auto e = s.find('e');
    if (e != std::string::npos)
    {
        // 1.99e+30 -> 1.99e30, 5e-05 -> 5e-5
        const bool negative = s[e + 1] == '-';
        size_t digits = e + 2;
        while (digits + 1 < s.size() && s[digits] == '0')
            ++digits;
        s = s.substr(0, e) + "e" + (negative ? "-" : "") + s.substr(digits);
    }
    return s + " kg";
}

inline std::string label_text(const mass& m)
{
    return m.label() ? *m.label() : format_mass(m.m());
}

// A mass as seen on screen. Larger priority wins a contested spot.
struct label_body
{
    size_t id = 0;
    float x = 0, y = 0;    // centre, in pixels
    float radius = 0;      // in pixels
    double priority = 0;
    bool labelled = true;  // false: ignored, so a dense field of unlabelled bodies does not hide the few that are named
};

struct label_size
{
    float width = 0, height = 0;
};

struct label_placement
{
    size_t id = 0;
    float left = 0, top = 0;
};

struct label_options
{
    float gap = 3.0f;               // between the body and its label
    float padding = 2.0f;           // kept clear around a label
    size_t max_labels = 300;
    size_t max_candidates = 3000;   // bodies examined, in priority order
    float cell = 32.0f;
    double cover_ratio = 0.01;      // a label may sit over a body below this fraction of its own priority (a planet over its small moons)
};

// Greedy, highest priority first. A label goes to the right of its body, else the left, and is dropped if it
// would leave the view, touch an already placed label, or cover another labelled body (other than one far less
// important, which loses its own label instead). So zooming out or a crowd leaves only the most important
// labels, and zooming in brings the rest back.
// `measure(id)` returns the text size and is only called for bodies that are actually considered.
template<typename Measure>
std::vector<label_placement> place_labels(std::vector<label_body> bodies, float view_width, float view_height, Measure measure, const label_options& options = {})
{
    std::vector<label_placement> placed;
    if (!(view_width > 0) || !(view_height > 0))
        return placed;

    bodies.erase(std::remove_if(bodies.begin(), bodies.end(), [&](const label_body& b) {
        return !b.labelled || !(b.x + b.radius >= 0 && b.y + b.radius >= 0 && b.x - b.radius <= view_width && b.y - b.radius <= view_height);
    }), bodies.end());

    std::vector<uint32_t> order(bodies.size());
    for (uint32_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return bodies[a].priority > bodies[b].priority;
    });

    // Bodies bucketed by screen cell so a label only tests its neighbours.
    const int columns = int(view_width / options.cell) + 1;
    const int rows = int(view_height / options.cell) + 1;
    std::vector<std::vector<uint32_t>> grid(size_t(columns) * size_t(rows));
    auto cell_range = [&](float low, float high, int count, int& first, int& last) {
        first = (std::max)(0, (std::min)(count - 1, int(std::floor(low / options.cell))));
        last = (std::max)(0, (std::min)(count - 1, int(std::floor(high / options.cell))));
    };
    for (uint32_t i = 0; i < bodies.size(); ++i)
    {
        const auto& b = bodies[i];
        const float r = (std::max)(b.radius, 1.5f);
        int c0, c1, r0, r1;
        cell_range(b.x - r, b.x + r, columns, c0, c1);
        cell_range(b.y - r, b.y + r, rows, r0, r1);
        for (int row = r0; row <= r1; ++row)
            for (int column = c0; column <= c1; ++column)
                grid[size_t(row) * size_t(columns) + size_t(column)].push_back(i);
    }

    struct rect { float left, top, right, bottom; };
    std::vector<rect> taken;

    auto blocked = [&](const rect& r, size_t own_id, double own_priority) {
        const float pad = options.padding;
        for (const auto& t : taken)
        {
            if (r.left < t.right + pad && r.right + pad > t.left && r.top < t.bottom + pad && r.bottom + pad > t.top)
                return true;
        }

        int c0, c1, r0, r1;
        cell_range(r.left - pad, r.right + pad, columns, c0, c1);
        cell_range(r.top - pad, r.bottom + pad, rows, r0, r1);
        for (int row = r0; row <= r1; ++row)
        {
            for (int column = c0; column <= c1; ++column)
            {
                for (const uint32_t i : grid[size_t(row) * size_t(columns) + size_t(column)])
                {
                    const auto& b = bodies[i];
                    if (b.id == own_id)
                        continue;
                    if (b.priority < own_priority * options.cover_ratio)
                        continue;
                    const float radius = (std::max)(b.radius, 1.5f) + pad;
                    const float dx = (std::max)((std::max)(r.left - b.x, b.x - r.right), 0.0f);
                    const float dy = (std::max)((std::max)(r.top - b.y, b.y - r.bottom), 0.0f);
                    if (dx * dx + dy * dy < radius * radius)
                        return true;
                }
            }
        }
        return false;
    };

    size_t examined = 0;
    for (const uint32_t index : order)
    {
        const auto& b = bodies[index];
        if (placed.size() >= options.max_labels || examined >= options.max_candidates)
            break;
        ++examined;

        const label_size size = measure(b.id);
        if (!(size.width > 0) || !(size.height > 0))
            continue;

        const float reach = (std::max)(b.radius, 1.5f) + options.gap;
        const float top = b.y - size.height / 2;
        const rect candidates[2] = {
            { b.x + reach, top, b.x + reach + size.width, top + size.height },
            { b.x - reach - size.width, top, b.x - reach, top + size.height },
        };
        for (const auto& r : candidates)
        {
            if (r.left < 0 || r.top < 0 || r.right > view_width || r.bottom > view_height)
                continue;
            if (blocked(r, b.id, b.priority))
                continue;
            taken.push_back(r);
            placed.push_back({ b.id, r.left, r.top });
            break;
        }
    }
    return placed;
}
