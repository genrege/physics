#pragma once

#include "simulation.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// The editable form of a simulation. Contains no Windows code so it can be unit tested.

struct editor_mass
{
    double m = 100.0;
    double r = 10.0;
    double e = 1.0;
    vector2 position;
    vector2 velocity;
    bool fixed = false;
    std::optional<uint32_t> colour;   // 0xRRGGBB; unset draws in the default colour
    std::optional<std::string> label; // UTF-8; unset shows the mass, empty shows nothing
};

struct editor_spring
{
    size_t a = 0;
    size_t b = 0;
    double k = 10.0;
    double l0 = 1.0;
    double min_factor = 0.5;
    double max_factor = 1.5;
    std::optional<uint32_t> colour;
};

struct editor_damper
{
    size_t a = 0;
    size_t b = 0;
    double k = 0.5;
    double l0 = 0.0;
};

struct editor_world
{
    double xmin = -1000.0;
    double xmax = 1000.0;
    double ymin = -1000.0;
    double ymax = 1000.0;
    double dt = 0.1;
    size_t iterations = 10;
    double min_gravitational_mass = 10000.0;
    double speed = 1.0;   // starting speed, in multiples of real time
    double softening = 0.0;   // gravity softening length; 0 is the exact inverse-square law

    // Edges masses bounce off (at the bounds above); the bottom is the floor.
    bool bounce_left = false;
    bool bounce_right = false;
    bool bounce_top = false;
    bool bounce_bottom = true;
};

struct editor_document
{
    editor_world world;
    std::vector<editor_mass> masses;
    std::vector<editor_spring> springs;
    std::vector<editor_damper> dampers;
};

// Values used for newly placed masses and connections.
struct editor_defaults
{
    editor_mass mass;
    double spring_k = 10.0;
    double damper_k = 0.5;
};

struct editor_selection
{
    std::set<size_t> masses;
    std::set<size_t> springs;
    std::set<size_t> dampers;

    bool empty() const { return masses.empty() && springs.empty() && dampers.empty(); }
};

// Optional fields are applied to every selected item; unset fields are left alone.
struct mass_edit
{
    std::optional<double> m, r, e, x, y, vx, vy;
    std::optional<bool> fixed;
    std::optional<uint32_t> colour;
    bool reset_colour = false;      // back to the default colour
    std::optional<std::string> label;
    bool reset_label = false;       // back to showing the mass
};

struct spring_edit
{
    std::optional<double> k, l0, min_factor, max_factor;
    std::optional<uint32_t> colour;
    bool reset_colour = false;
};

struct damper_edit
{
    std::optional<double> k, l0;
};

enum class connection_kind { spring, damper, both };
enum class select_mode { replace, toggle, add };

// All springs and dampers joining the same two masses.
struct connection_hit
{
    size_t a = 0;
    size_t b = 0;
    std::vector<size_t> springs;
    std::vector<size_t> dampers;
};

editor_document document_from_simulation(const simulation& sim, const std::vector<mass_state>& states);

std::shared_ptr<simulation> build_simulation(const editor_document& doc, std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers);

class editor_model
{
public:
    editor_model();

    // Replaces the document and clears selection and undo history.
    void reset(editor_document doc);

    const editor_document& document() const { return doc_; }
    const editor_selection& selection() const { return selection_; }
    editor_defaults& defaults() { return defaults_; }
    const editor_defaults& defaults() const { return defaults_; }

    bool dirty() const { return dirty_; }
    void mark_clean() { dirty_ = false; }

    // Hit testing, in world units. `extra` widens the target (e.g. a few pixels).
    std::optional<size_t> pick_mass(vector2 point, double extra) const;
    std::optional<connection_hit> pick_connection(vector2 point, double tolerance) const;
    std::vector<size_t> masses_in_rect(vector2 corner1, vector2 corner2) const;

    void select_mass(size_t index, select_mode mode);
    void select_masses(const std::vector<size_t>& indices, select_mode mode);
    void select_connection(const connection_hit& hit, select_mode mode);
    void select_all();
    void clear_selection();

    // Continuous edits (dragging): call begin_gesture() once, then the mutators any number of times.
    void begin_gesture();
    void move_selection(vector2 delta);
    void set_velocity(size_t mass, vector2 velocity);

    // Discrete edits; each is one undo step.
    size_t add_mass(vector2 position);
    bool connect(size_t a, size_t b, connection_kind kind);
    void delete_selection();
    bool edit_masses(const mass_edit& edit, std::string& error);
    bool edit_connections(const spring_edit& spring, const damper_edit& damper, std::string& error);
    bool edit_world(const editor_world& world, std::string& error);

    // The clipboard text is a complete simulation XML holding the selected masses and the springs and
    // dampers between them. Returns an empty string when nothing is selected.
    std::string copy_selection() const;
    std::string cut_selection();
    // Paste moves the copied masses so their centre lands on `anchor`, or shifts them by `delta`.
    bool paste_at(const std::string& xml, vector2 anchor, std::string& error);
    bool paste_offset(const std::string& xml, vector2 delta, std::string& error);
    bool duplicate_selection();

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    void undo();
    void redo();

private:
    struct snapshot
    {
        editor_document doc;
        editor_selection selection;
    };

    void push_undo();
    bool parse_fragment(const std::string& xml, editor_document& fragment, std::string& error) const;
    void append_fragment(const editor_document& fragment, vector2 translation);

    editor_document doc_;
    editor_selection selection_;
    editor_defaults defaults_;
    std::vector<snapshot> undo_;
    std::vector<snapshot> redo_;
    bool dirty_ = false;
};
