#pragma once

#include "model_system.h"
#include "vector2.h"
#include <cmath>
#include <vector>
#include <utility>
#include <unordered_map>
#include <cstdint>
#include <algorithm>

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

    double  angular_velocity_;
    double  angular_position_;

    bool fixed_;    //if true doesn't move
};

struct spring_state
{
    spring_state(bool broken) : broken_(false) {}

    bool broken_;
};

struct damper_state
{
    damper_state(bool broken) : broken_(false) {}

    bool broken_;
};

class simulation
{
public:
    simulation(const model_system& model, double xmin, double xmax, double ymin, double ymax, double dt, size_t iterations_per_update, double min_gravitational_mass = 10000.0) : model_(model), xmin_(xmin), xmax_(xmax), ymin_(ymin), ymax_(ymax), dt_(dt), iterations_per_update_(iterations_per_update), min_gravitational_mass_(min_gravitational_mass) {}

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

    void prepare_for_update(std::vector<mass_state>& state)
    {
        for (auto& s : state)
        {
            s.force_ = vector2();
            s.acceleration_ = vector2();
        }
    }

    void update_broken_springs_and_dampers(const std::vector<mass_state>& mass_state, std::vector<spring_state>& spring_state, std::vector<damper_state>& damper_state)
    {

        const auto count_springs = model_.springs().size();
        for (size_t i = 0; i < count_springs; ++i)
        {
            const auto& spring = model_.springs()[i];
            const auto id_mass1 = spring.id_mass1();
            const auto id_mass2 = spring.id_mass2();
            const auto& position1 = mass_state[id_mass1].position_;
            const auto& position2 = mass_state[id_mass2].position_;
            const auto length = position1.distance(position2);

            if (length < spring.min_length() || length > spring.max_length())
            {
                spring_state[i].broken_ = true;

                for (size_t j = 0; j < damper_state.size(); ++j)
                {
                    const auto& damper = model_.dampers()[j];
                    if (damper.id_mass1() == id_mass1 && damper.id_mass2() == id_mass2)
                    {
                        damper_state[j].broken_ = true;
                    }
                }
            }
        }
    }

    void update_gravitational_force(std::vector<mass_state>& state) const
    {
        const auto count_masses = model_.masses().size();

        for (size_t i = 0; i < count_masses; ++i)
        {
            const auto& mass_m1 = model_.masses()[i].m();
            const auto& position1 = state[i].position_;
            const auto G_mass_m1 = constants::G * mass_m1;

            for (size_t j = i + 1; j < count_masses; ++j)
            {
                const auto& mass_m2 = model_.masses()[j].m();

                if (mass_m1 + mass_m2 < min_gravitational_mass_)
                    continue;

                const auto& position2 = state[j].position_;

                //Unit vector m1->m2
                const auto& u = (position2 - position1).unit_vector();

                //Distance m1->m2, constrained by radii of masses
                const auto distance = position1.distance(position2);

                //Calculate scalar gravitational force acting on m1 due to m2 and apply to unit vector
                const auto scalar_force = G_mass_m1 * (mass_m2 / (distance * distance));
                const auto sfu = scalar_force * u;
                state[i].force_ += sfu;

                //m2 has the opposite force
                state[j].force_ -= sfu;
            }
        }
    }

    void update_spring_force(std::vector<mass_state>& state, const std::vector<spring_state>& spring_states) const
    {
        const auto count_springs = model_.springs().size();

        for (size_t i = 0; i < count_springs; ++i)
        {
            if (spring_states[i].broken_)
                continue;

            const auto& spring = model_.springs()[i];
            const auto id_mass1 = spring.id_mass1();
            const auto id_mass2 = spring.id_mass2();
            const auto& position1 = state[id_mass1].position_;
            const auto& position2 = state[id_mass2].position_;
            const auto length = position1.distance(position2);

            const auto& velocity1 = state[id_mass1].velocity_;
            const auto& velocity2 = state[id_mass2].velocity_;

            const auto scalar_force = spring.force(length);

            const auto& u = (position2 - position1).unit_vector();

            state[id_mass1].force_ += scalar_force * u;
            state[id_mass2].force_ -= scalar_force * u;
        }
    }

    void update_damper_force(std::vector<mass_state>& state, const std::vector<damper_state>& damper_states) const
    {
        const auto count_dampers = model_.dampers().size();

        for (size_t i = 0; i < count_dampers; ++i)
        {
            if (damper_states[i].broken_)
                continue;

            const auto& damper = model_.dampers()[i];
            const auto id_mass1 = damper.id_mass1();
            const auto id_mass2 = damper.id_mass2();
            const auto& A = state[id_mass1].position_;
            const auto& B = state[id_mass2].position_;
            const auto& vA = state[id_mass1].velocity_;
            const auto& vB = state[id_mass2].velocity_;

            const auto damping_force = dot_product(damper.k() * (vA - vB), ((B - A) / (B - A).modulus()));

            const auto& u = (B - A).unit_vector();

            state[id_mass1].force_ -= damping_force * u;
            state[id_mass2].force_ += damping_force * u;
        }
    }

    void update_spatial(std::vector<mass_state>& state, double dt) const
    {
        const auto count_masses = model_.masses().size();

        for (size_t i = 0; i < count_masses; ++i)
        {
            auto& mass_state = state[i];

            const auto& mass = model_.masses()[i];
            mass_state.acceleration_ = state[i].force_ / mass.m();

            if (!state[i].fixed_)
            {
                mass_state.velocity_ += mass_state.acceleration_ * dt;
                mass_state.position_ += mass_state.velocity_ * dt;
            }
        }
    }

    void update_floor(std::vector<mass_state>& state) const
    {
        for (size_t i = 0; i < state.size(); ++i)
        {
            auto& s = state[i];
            if (s.position_.y() + model_.masses()[i].r() > ymax())
            {
                s.velocity_ = vector2(0.8 * s.velocity_.x(), -0.92f * fabs(s.velocity_.y()));
            }
        }
    }

    void update_collisions(std::vector<mass_state>& state)
    {
        const auto& masses = model_.masses();
        const size_t count = masses.size();
        if (count <= 1) return;

        // choose cell size based on maximum mass radius (covering likely collision range)
        double max_r = 0.0;
        for (size_t ii = 0; ii < masses.size(); ++ii) { const auto& m = masses[ii]; if (m.r() > max_r) max_r = m.r(); }
        double cell_size = 2.0 * max_r;
        if (cell_size < 1e-3) cell_size = 1e-3; // at least small positive

        // spatial hash using 64-bit key combining x and y
        std::unordered_map<int64_t, std::vector<size_t>> spatial_hash;
        spatial_hash.reserve(count * 2);

        auto cell_key = [](int64_t x, int64_t y) -> int64_t {
            return (x << 32) ^ (y & 0xffffffffLL);
            };

        // populate hash
        for (size_t i = 0; i < count; ++i)
        {
            const auto& pos = state[i].position_;
            int64_t cx = static_cast<int64_t>(std::floor(pos.x() / cell_size));
            int64_t cy = static_cast<int64_t>(std::floor(pos.y() / cell_size));
            spatial_hash[cell_key(cx, cy)].push_back(i);
        }

        // neighbor offsets
        const int offs[3] = { -1, 0, 1 };

        // narrow-phase: test in cell + neighbors
        for (size_t i = 0; i < count; ++i)
        {
            const auto p1 = state[i].position_;
            const auto mass1 = masses[i];

            int64_t cx = static_cast<int64_t>(std::floor(p1.x() / cell_size));
            int64_t cy = static_cast<int64_t>(std::floor(p1.y() / cell_size));

            for (int dx_cell : offs)
            {
                for (int dy_cell : offs)
                {
                    int64_t nk = cell_key(cx + dx_cell, cy + dy_cell);
                    auto it = spatial_hash.find(nk);
                    if (it == spatial_hash.end()) continue;
                    const auto& cell = it->second;
                    for (size_t idx = 0; idx < cell.size(); ++idx)
                    {
                        size_t j = cell[idx];
                        if (j <= i) continue; // avoid duplicate checks and self

                        const auto p2 = state[j].position_;

                        // compute delta and squared distance once
                        const double dx = p2.x() - p1.x();
                        const double dy = p2.y() - p1.y();
                        const double r2 = dx * dx + dy * dy;

                        if (r2 <= 1.0e-12)
                            continue; // skip extremely close / identical positions

                        const auto& mass2 = masses[j];
                        const double rsum = mass1.r() + mass2.r();
                        if (r2 > rsum * rsum)
                            continue; // not colliding

                        const double m1 = mass1.m();
                        const double m2 = mass2.m();
                        const double M = m1 + m2;

                        const double distance = std::sqrt(r2);

                        const double e = (distance - mass1.r() - mass2.r());

                        const double inv_r = 1.0 / distance;
                        const double ux = dx * inv_r; // unit vector x (p1->p2)
                        const double uy = dy * inv_r; // unit vector y

                        // position correction: du = e * u
                        const double dux = e * ux;
                        const double duy = e * uy;

                        const double dr = m2 / M;
                        // apply position corrections using vector2 arithmetic
                        state[i].position_ = state[i].position_ + vector2(dr * dux, dr * duy);
                        state[j].position_ = state[j].position_ - vector2((1 - dr) * dux, (1 - dr) * duy);

                        const auto elastic_coeff = mass1.e() * mass2.e();
                        const auto ideal_distance = mass1.r() + mass2.r();
                        const auto ideal_distance_squared = ideal_distance * ideal_distance;

                        const auto v1 = state[i].velocity_;
                        const auto v2 = state[j].velocity_;

                        const double recip_ideal_distance_squared = 1.0 / ideal_distance_squared;

                        // compute p1-p2 vector for velocity update
                        const vector2 dp = vector2(p1.x() - p2.x(), p1.y() - p2.y());

                        const double dot1 = dot_product(v1 - v2, dp);
                        const double dot2 = dot_product(v2 - v1, -dp);

                        state[i].velocity_ = v1 - (2.0 * elastic_coeff * (m2 / M) * dot1 * recip_ideal_distance_squared) * dp;
                        state[j].velocity_ = v2 - (2.0 * elastic_coeff * (m1 / M) * dot2 * recip_ideal_distance_squared) * (-dp);
                    }
                }
            }
        }
    }

    std::vector<vector2> gravitational_vector_field(const std::vector<vector2>& points) const
    {
        std::vector<vector2> field;

        for (const auto& pt : points)
        {
            field.emplace_back(pt.x(), pt.y());
        }

        return field;
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

private:
    double dt_;
    size_t iterations_per_update_;
    double xmin_;
    double xmax_;
    double ymin_;
    double ymax_;
    double min_gravitational_mass_; //if the sum of both masses are less than this value, gravitational force not calculated

    model_system    model_; //the system being simulated
};

