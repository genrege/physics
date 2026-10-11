#include "editor_model.h"
#include "simulation_xml.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace
{
    constexpr size_t max_undo_steps = 50;

    bool finite(double v) { return std::isfinite(v); }

    double segment_distance(vector2 p, vector2 a, vector2 b)
    {
        const vector2 ab = b - a;
        const double length_squared = ab.sum_squares();
        if (length_squared == 0.0)
            return p.distance(a);
        double t = dot_product(p - a, ab) / length_squared;
        t = std::max(0.0, std::min(1.0, t));
        return p.distance(a + ab * t);
    }

    std::pair<size_t, size_t> ordered(size_t a, size_t b)
    {
        return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
    }

    bool valid_world(const editor_world& w, std::string& error)
    {
        if (!finite(w.xmin) || !finite(w.xmax) || !finite(w.ymin) || !finite(w.ymax) || !(w.xmax > w.xmin) || !(w.ymax > w.ymin))
        {
            error = "World bounds need xmax > xmin and ymax > ymin";
            return false;
        }
        if (!finite(w.dt) || !(w.dt > 0))
        {
            error = "Time step must be greater than 0";
            return false;
        }
        if (w.iterations < 1)
        {
            error = "Iterations must be at least 1";
            return false;
        }
        if (!finite(w.min_gravitational_mass) || w.min_gravitational_mass < 0)
        {
            error = "Gravity threshold must be 0 or more";
            return false;
        }
        if (!finite(w.speed) || !(w.speed > 0))
        {
            error = "Speed must be greater than 0";
            return false;
        }
        if (!finite(w.softening) || w.softening < 0)
        {
            error = "Softening must be 0 or more";
            return false;
        }
        return true;
    }
}

editor_document document_from_simulation(const simulation& sim, const std::vector<mass_state>& states)
{
    editor_document doc;
    doc.world = { sim.xmin(), sim.xmax(), sim.ymin(), sim.ymax(), sim.dt(), sim.iterations_per_update(), sim.min_gravitational_mass() };
    doc.world.bounce_left = sim.walls().left;
    doc.world.bounce_right = sim.walls().right;
    doc.world.bounce_top = sim.walls().top;
    doc.world.bounce_bottom = sim.walls().bottom;
    doc.world.speed = sim.speed();
    doc.world.softening = sim.softening();

    const auto& masses = sim.model().masses();
    for (size_t i = 0; i < masses.size(); ++i)
        doc.masses.push_back({ masses[i].m(), masses[i].r(), masses[i].e(), states[i].position_, states[i].velocity_, states[i].fixed_, masses[i].colour(), masses[i].label() });

    for (const auto& s : sim.model().springs())
        doc.springs.push_back({ s.id_mass1(), s.id_mass2(), s.k(), s.l0(), s.min_factor(), s.max_factor(), s.colour() });

    for (const auto& d : sim.model().dampers())
        doc.dampers.push_back({ d.id_mass1(), d.id_mass2(), d.k(), d.l0() });

    return doc;
}

std::shared_ptr<simulation> build_simulation(const editor_document& doc, std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states)
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    mass_states.clear();
    spring_states.clear();
    damper_states.clear();

    for (const auto& m : doc.masses)
    {
        masses.emplace_back(m.m, m.r, m.e, m.colour, m.label);
        mass_states.emplace_back(m.position, vector2{}, m.velocity, vector2{}, 0.0, 0.0, m.fixed);
    }
    for (const auto& s : doc.springs)
    {
        springs.emplace_back(s.a, s.b, s.k, s.l0, s.min_factor, s.max_factor, s.colour);
        spring_states.emplace_back(false);
    }
    for (const auto& d : doc.dampers)
    {
        dampers.emplace_back(d.a, d.b, d.k, d.l0);
        damper_states.emplace_back(false);
    }

    const auto& w = doc.world;
    const wall_flags walls = { w.bounce_left, w.bounce_right, w.bounce_top, w.bounce_bottom };
    return std::make_shared<simulation>(model_system(masses, springs, dampers), w.xmin, w.xmax, w.ymin, w.ymax, w.dt, w.iterations, w.min_gravitational_mass, walls, w.speed, w.softening);
}

editor_model::editor_model()
{
    defaults_.mass.m = 100.0;
    defaults_.mass.r = 20.0;
}

void editor_model::reset(editor_document doc)
{
    doc_ = std::move(doc);
    selection_ = {};
    undo_.clear();
    redo_.clear();
    dirty_ = false;
}

void editor_model::push_undo()
{
    undo_.push_back({ doc_, selection_ });
    if (undo_.size() > max_undo_steps)
        undo_.erase(undo_.begin());
    redo_.clear();
    dirty_ = true;
}

void editor_model::undo()
{
    if (undo_.empty())
        return;
    redo_.push_back({ doc_, selection_ });
    doc_ = std::move(undo_.back().doc);
    selection_ = std::move(undo_.back().selection);
    undo_.pop_back();
    dirty_ = true;
}

void editor_model::redo()
{
    if (redo_.empty())
        return;
    undo_.push_back({ doc_, selection_ });
    doc_ = std::move(redo_.back().doc);
    selection_ = std::move(redo_.back().selection);
    redo_.pop_back();
    dirty_ = true;
}

std::optional<size_t> editor_model::pick_mass(vector2 point, double extra) const
{
    // Later masses are drawn on top, so they win.
    for (size_t i = doc_.masses.size(); i-- > 0;)
    {
        if (point.distance(doc_.masses[i].position) <= doc_.masses[i].r + extra)
            return i;
    }
    return std::nullopt;
}

std::optional<connection_hit> editor_model::pick_connection(vector2 point, double tolerance) const
{
    double best = tolerance;
    std::optional<std::pair<size_t, size_t>> best_pair;

    auto consider = [&](size_t a, size_t b) {
        const double d = segment_distance(point, doc_.masses[a].position, doc_.masses[b].position);
        if (d <= best)
        {
            best = d;
            best_pair = ordered(a, b);
        }
    };

    for (const auto& s : doc_.springs)
        consider(s.a, s.b);
    for (const auto& d : doc_.dampers)
        consider(d.a, d.b);

    if (!best_pair)
        return std::nullopt;

    connection_hit hit;
    hit.a = best_pair->first;
    hit.b = best_pair->second;
    for (size_t i = 0; i < doc_.springs.size(); ++i)
    {
        if (ordered(doc_.springs[i].a, doc_.springs[i].b) == *best_pair)
            hit.springs.push_back(i);
    }
    for (size_t i = 0; i < doc_.dampers.size(); ++i)
    {
        if (ordered(doc_.dampers[i].a, doc_.dampers[i].b) == *best_pair)
            hit.dampers.push_back(i);
    }
    return hit;
}

std::vector<size_t> editor_model::masses_in_rect(vector2 corner1, vector2 corner2) const
{
    const double x0 = std::min(corner1.x(), corner2.x());
    const double x1 = std::max(corner1.x(), corner2.x());
    const double y0 = std::min(corner1.y(), corner2.y());
    const double y1 = std::max(corner1.y(), corner2.y());

    std::vector<size_t> result;
    for (size_t i = 0; i < doc_.masses.size(); ++i)
    {
        const auto& p = doc_.masses[i].position;
        if (p.x() >= x0 && p.x() <= x1 && p.y() >= y0 && p.y() <= y1)
            result.push_back(i);
    }
    return result;
}

void editor_model::select_mass(size_t index, select_mode mode)
{
    select_masses({ index }, mode);
}

void editor_model::select_masses(const std::vector<size_t>& indices, select_mode mode)
{
    if (mode == select_mode::replace)
        selection_ = {};

    for (const auto i : indices)
    {
        if (i >= doc_.masses.size())
            continue;
        if (mode == select_mode::toggle && selection_.masses.count(i))
            selection_.masses.erase(i);
        else
            selection_.masses.insert(i);
    }
}

void editor_model::select_connection(const connection_hit& hit, select_mode mode)
{
    const bool already = !hit.springs.empty() ? selection_.springs.count(hit.springs.front()) > 0 : (!hit.dampers.empty() && selection_.dampers.count(hit.dampers.front()) > 0);

    if (mode == select_mode::replace)
        selection_ = {};

    const bool remove = mode == select_mode::toggle && already;
    for (const auto i : hit.springs)
    {
        if (remove) selection_.springs.erase(i); else selection_.springs.insert(i);
    }
    for (const auto i : hit.dampers)
    {
        if (remove) selection_.dampers.erase(i); else selection_.dampers.insert(i);
    }
}

void editor_model::select_all()
{
    selection_ = {};
    for (size_t i = 0; i < doc_.masses.size(); ++i)
        selection_.masses.insert(i);
}

void editor_model::clear_selection()
{
    selection_ = {};
}

void editor_model::begin_gesture()
{
    push_undo();
}

void editor_model::move_selection(vector2 delta)
{
    for (const auto i : selection_.masses)
        doc_.masses[i].position += delta;
}

void editor_model::set_velocity(size_t mass, vector2 velocity)
{
    if (mass < doc_.masses.size())
        doc_.masses[mass].velocity = velocity;
}

size_t editor_model::add_mass(vector2 position)
{
    push_undo();
    editor_mass m = defaults_.mass;
    m.position = position;
    doc_.masses.push_back(m);
    selection_ = {};
    selection_.masses.insert(doc_.masses.size() - 1);
    return doc_.masses.size() - 1;
}

bool editor_model::connect(size_t a, size_t b, connection_kind kind)
{
    if (a == b || a >= doc_.masses.size() || b >= doc_.masses.size())
        return false;

    const auto pair = ordered(a, b);
    bool has_spring = false;
    bool has_damper = false;
    for (const auto& s : doc_.springs)
        has_spring = has_spring || ordered(s.a, s.b) == pair;
    for (const auto& d : doc_.dampers)
        has_damper = has_damper || ordered(d.a, d.b) == pair;

    const bool add_spring = kind != connection_kind::damper && !has_spring;
    const bool add_damper = kind != connection_kind::spring && !has_damper;
    if (!add_spring && !add_damper)
        return false;

    push_undo();

    // Start at rest: the spring's natural length is the current distance.
    double length = doc_.masses[a].position.distance(doc_.masses[b].position);
    if (length <= 0.0)
        length = doc_.masses[a].r + doc_.masses[b].r;
    if (length <= 0.0)
        length = 1.0;

    if (add_spring)
        doc_.springs.push_back({ a, b, defaults_.spring_k, length, 0.5, 1.5 });
    if (add_damper)
        doc_.dampers.push_back({ a, b, defaults_.damper_k, length });
    return true;
}

void editor_model::delete_selection()
{
    if (selection_.empty())
        return;
    push_undo();

    const size_t count = doc_.masses.size();
    std::vector<char> removed(count, 0);
    for (const auto i : selection_.masses)
        removed[i] = 1;

    std::vector<size_t> new_index(count, 0);
    size_t next = 0;
    for (size_t i = 0; i < count; ++i)
        new_index[i] = removed[i] ? 0 : next++;

    std::vector<editor_spring> springs;
    for (size_t i = 0; i < doc_.springs.size(); ++i)
    {
        auto s = doc_.springs[i];
        if (selection_.springs.count(i) || removed[s.a] || removed[s.b])
            continue;
        s.a = new_index[s.a];
        s.b = new_index[s.b];
        springs.push_back(s);
    }

    std::vector<editor_damper> dampers;
    for (size_t i = 0; i < doc_.dampers.size(); ++i)
    {
        auto d = doc_.dampers[i];
        if (selection_.dampers.count(i) || removed[d.a] || removed[d.b])
            continue;
        d.a = new_index[d.a];
        d.b = new_index[d.b];
        dampers.push_back(d);
    }

    std::vector<editor_mass> masses;
    for (size_t i = 0; i < count; ++i)
    {
        if (!removed[i])
            masses.push_back(doc_.masses[i]);
    }

    doc_.masses = std::move(masses);
    doc_.springs = std::move(springs);
    doc_.dampers = std::move(dampers);
    selection_ = {};
}

bool editor_model::edit_masses(const mass_edit& edit, std::string& error)
{
    if (selection_.masses.empty())
    {
        error = "No masses are selected";
        return false;
    }

    auto positive = [&](const std::optional<double>& v, const char* what) {
        if (v && !(finite(*v) && *v > 0))
        {
            error = std::string(what) + " must be greater than 0";
            return false;
        }
        return true;
    };
    auto non_negative = [&](const std::optional<double>& v, const char* what) {
        if (v && !(finite(*v) && *v >= 0))
        {
            error = std::string(what) + " must be 0 or more";
            return false;
        }
        return true;
    };
    auto number = [&](const std::optional<double>& v, const char* what) {
        if (v && !finite(*v))
        {
            error = std::string(what) + " must be a number";
            return false;
        }
        return true;
    };
    if (!positive(edit.m, "Mass") || !non_negative(edit.r, "Radius") || !non_negative(edit.e, "Elasticity") ||
        !number(edit.x, "x") || !number(edit.y, "y") || !number(edit.vx, "vx") || !number(edit.vy, "vy"))
        return false;

    push_undo();
    for (const auto i : selection_.masses)
    {
        auto& m = doc_.masses[i];
        if (edit.m) m.m = *edit.m;
        if (edit.r) m.r = *edit.r;
        if (edit.e) m.e = *edit.e;
        if (edit.x) m.position = vector2(*edit.x, m.position.y());
        if (edit.y) m.position = vector2(m.position.x(), *edit.y);
        if (edit.vx) m.velocity = vector2(*edit.vx, m.velocity.y());
        if (edit.vy) m.velocity = vector2(m.velocity.x(), *edit.vy);
        if (edit.fixed) m.fixed = *edit.fixed;
        if (edit.colour) m.colour = edit.colour;
        if (edit.reset_colour) m.colour.reset();
        if (edit.label) m.label = edit.label;
        if (edit.reset_label) m.label.reset();
    }
    return true;
}

bool editor_model::edit_connections(const spring_edit& spring, const damper_edit& damper, std::string& error)
{
    if (selection_.springs.empty() && selection_.dampers.empty())
    {
        error = "No springs or dampers are selected";
        return false;
    }

    if (spring.k && !finite(*spring.k)) { error = "Spring k must be a number"; return false; }
    if (spring.l0 && !(finite(*spring.l0) && *spring.l0 > 0)) { error = "Rest length must be greater than 0"; return false; }
    if (damper.k && !finite(*damper.k)) { error = "Damper k must be a number"; return false; }
    if (damper.l0 && !finite(*damper.l0)) { error = "Damper length must be a number"; return false; }

    auto edited = doc_.springs;
    for (const auto i : selection_.springs)
    {
        if (spring.k) edited[i].k = *spring.k;
        if (spring.l0) edited[i].l0 = *spring.l0;
        if (spring.min_factor) edited[i].min_factor = *spring.min_factor;
        if (spring.max_factor) edited[i].max_factor = *spring.max_factor;
        if (spring.colour) edited[i].colour = spring.colour;
        if (spring.reset_colour) edited[i].colour.reset();
        if (!(finite(edited[i].min_factor) && finite(edited[i].max_factor) && edited[i].min_factor >= 0 && edited[i].max_factor > edited[i].min_factor))
        {
            error = "Break limits need 0 <= min < max";
            return false;
        }
    }

    push_undo();
    doc_.springs = std::move(edited);
    for (const auto i : selection_.dampers)
    {
        if (damper.k) doc_.dampers[i].k = *damper.k;
        if (damper.l0) doc_.dampers[i].l0 = *damper.l0;
    }
    return true;
}

bool editor_model::edit_world(const editor_world& world, std::string& error)
{
    if (!valid_world(world, error))
        return false;
    push_undo();
    doc_.world = world;
    return true;
}

std::string editor_model::copy_selection() const
{
    if (selection_.masses.empty())
        return {};

    editor_document fragment;
    fragment.world = doc_.world;

    std::map<size_t, size_t> new_index;
    for (const auto i : selection_.masses)
    {
        new_index[i] = fragment.masses.size();
        fragment.masses.push_back(doc_.masses[i]);
    }

    for (auto s : doc_.springs)
    {
        const auto a = new_index.find(s.a);
        const auto b = new_index.find(s.b);
        if (a == new_index.end() || b == new_index.end())
            continue;
        s.a = a->second;
        s.b = b->second;
        fragment.springs.push_back(s);
    }
    for (auto d : doc_.dampers)
    {
        const auto a = new_index.find(d.a);
        const auto b = new_index.find(d.b);
        if (a == new_index.end() || b == new_index.end())
            continue;
        d.a = a->second;
        d.b = b->second;
        fragment.dampers.push_back(d);
    }

    std::vector<mass_state> ms;
    std::vector<spring_state> ss;
    std::vector<damper_state> ds;
    const auto sim = build_simulation(fragment, ms, ss, ds);
    return simulation_to_xml(*sim, ms);
}

std::string editor_model::cut_selection()
{
    auto text = copy_selection();
    if (!text.empty())
        delete_selection();
    return text;
}

bool editor_model::parse_fragment(const std::string& xml, editor_document& fragment, std::string& error) const
{
    std::vector<mass_state> ms;
    std::vector<spring_state> ss;
    std::vector<damper_state> ds;
    const auto sim = load_simulation_xml_text(xml, ms, ss, ds, error);
    if (!sim)
        return false;
    if (ms.empty())
    {
        error = "The clipboard contains no masses";
        return false;
    }
    fragment = document_from_simulation(*sim, ms);
    return true;
}

void editor_model::append_fragment(const editor_document& fragment, vector2 translation)
{
    push_undo();

    const size_t base = doc_.masses.size();
    selection_ = {};
    for (auto m : fragment.masses)
    {
        m.position += translation;
        doc_.masses.push_back(m);
        selection_.masses.insert(doc_.masses.size() - 1);
    }
    for (auto s : fragment.springs)
    {
        s.a += base;
        s.b += base;
        doc_.springs.push_back(s);
    }
    for (auto d : fragment.dampers)
    {
        d.a += base;
        d.b += base;
        doc_.dampers.push_back(d);
    }
}

bool editor_model::paste_at(const std::string& xml, vector2 anchor, std::string& error)
{
    editor_document fragment;
    if (!parse_fragment(xml, fragment, error))
        return false;

    vector2 centre;
    for (const auto& m : fragment.masses)
        centre += m.position;
    centre = centre / double(fragment.masses.size());

    append_fragment(fragment, anchor - centre);
    return true;
}

bool editor_model::paste_offset(const std::string& xml, vector2 delta, std::string& error)
{
    editor_document fragment;
    if (!parse_fragment(xml, fragment, error))
        return false;
    append_fragment(fragment, delta);
    return true;
}

bool editor_model::duplicate_selection()
{
    const auto text = copy_selection();
    if (text.empty())
        return false;

    double largest = 0.0;
    for (const auto i : selection_.masses)
        largest = std::max(largest, doc_.masses[i].r);
    const double offset = largest > 0.0 ? 2.5 * largest : 1.0;

    std::string error;
    return paste_offset(text, vector2(offset, offset), error);
}
