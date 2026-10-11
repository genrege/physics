#pragma once

#include "model_system.h"
#include "vector2.h"
#include <vector>

struct mass_state
{
    mass_state() = default;
    mass_state(const vector2& position, const vector2& force, const vector2& velocity, const vector2& acceleration, double angular_velocity, double angular_position, bool fixed)
        : position_(position), force_(force), velocity_(velocity), acceleration_(acceleration), angular_velocity_(angular_velocity), angular_position_(angular_position), fixed_(fixed)
    {
    }

    vector2 position_;
    vector2 force_;
    vector2 velocity_;
    vector2 acceleration_;

    double  angular_velocity_ = 0.0;
    double  angular_position_ = 0.0;

    bool fixed_ = false;    //if true doesn't move
};

struct spring_state
{
    spring_state(bool broken) : broken_(broken) {}

    bool broken_;
};

struct damper_state
{
    damper_state(bool broken) : broken_(broken) {}

    bool broken_;
};

// Which world edges masses bounce off; the bottom edge has always been the floor, so it is on by default.
struct wall_flags
{
    bool left = false;
    bool right = false;
    bool top = false;
    bool bottom = true;
};

// Describes the system being simulated (model, world bounds, time step); engines advance the state.
class simulation
{
public:
    simulation(const model_system& model, double xmin, double xmax, double ymin, double ymax, double dt, size_t iterations_per_update, double min_gravitational_mass = 10000.0, wall_flags walls = {}, double speed = 0.0, double softening = 0.0) : model_(model), xmin_(xmin), xmax_(xmax), ymin_(ymin), ymax_(ymax), dt_(dt), iterations_per_update_(iterations_per_update), min_gravitational_mass_(min_gravitational_mass), walls_(walls), speed_(speed > 0.0 ? speed : dt * double(iterations_per_update) * 60.0), softening_(softening) {}

    static size_t add_mass(double m, double r, vector2 position, vector2 velocity, vector2 acceleration, bool fixed, std::vector<mass>& masses, std::vector<mass_state>& state)
    {
        masses.emplace_back(m, r);
        state.emplace_back(position, vector2{}, velocity, acceleration, 0.0f, 0.0f, fixed);
        return masses.size() - 1;
    }

    static size_t add_mass(double m, double r, double e, vector2 position, vector2 velocity, vector2 acceleration, bool fixed, std::vector<mass>& masses, std::vector<mass_state>& state)
    {
        masses.emplace_back(m, r, e);
        state.emplace_back(position, vector2{}, velocity, acceleration, 0.0f, 0.0f, fixed);
        return masses.size() - 1;
    }

    static size_t add_spring(size_t id_mass1, size_t id_mass2, bool working, double k, double l0, std::vector<spring>& springs, std::vector<spring_state>& state)
    {
        springs.emplace_back(id_mass1, id_mass2, k, l0);
        state.emplace_back(false);
        return springs.size() - 1;
    }

    static size_t add_damper(size_t id_mass1, size_t id_mass2, const spring& spring, std::vector<damper>& dampers, std::vector<damper_state>& state)
    {
        dampers.emplace_back(id_mass1, id_mass2, spring);
        state.emplace_back(false);
        return dampers.size() - 1;
    }

    static size_t add_damper(size_t id_mass1, size_t id_mass2, double k, double l0, std::vector<damper>& dampers, std::vector<damper_state>& state)
    {
        dampers.emplace_back(id_mass1, id_mass2, k, l0);
        state.emplace_back(false);
        return dampers.size() - 1;
    }

    const auto& model() const
    {
        return model_;
    }

    double dt() const { return dt_; }
    size_t iterations_per_update() const { return iterations_per_update_; }

    double xmin() const { return xmin_; }
    double xmax() const { return xmax_; }
    double ymin() const { return ymin_; }
    double ymax() const { return ymax_; }

    //if the sum of both masses are less than this value, gravitational force not calculated
    double min_gravitational_mass() const { return min_gravitational_mass_; }

    const wall_flags& walls() const { return walls_; }

    // Starting speed as a multiple of real time (simulated seconds per wall-clock second). When a simulation does
    // not give one it is the speed the old fixed 'steps per frame' gave at 60 frames per second.
    double speed() const { return speed_; }

    // Gravity uses G m1 m2 d / (d^2 + softening^2)^1.5, which stays finite when masses pass close together.
    // Zero is the exact inverse-square law.
    double softening() const { return softening_; }

private:
    double dt_;
    size_t iterations_per_update_;
    double xmin_;
    double xmax_;
    double ymin_;
    double ymax_;
    double min_gravitational_mass_;
    wall_flags walls_;
    double speed_;
    double softening_;

    model_system    model_; //the system being simulated
};
