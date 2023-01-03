#pragma once

#include "model_system.h"
#include <cmath>
#include <vector>

struct mass_state
{
    mass_state() = default;
    mass_state(const double2& position, const double2& force, const double2& velocity, const double2& acceleration, double angular_velocity, double angular_position, bool fixed)
        : position_(position), force_(force), velocity_(velocity), acceleration_(acceleration), angular_velocity_(angular_velocity), angular_position_(angular_position), fixed_(fixed)
    {}

    double2 position_;
    double2 force_;
    double2 velocity_;
    double2 acceleration_;
    
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

    static size_t add_mass(double m, double r, double2 position, double2 velocity, double2 acceleration, bool fixed, std::vector<mass>& masses, std::vector<mass_state>& state)
    {
        masses.emplace_back(m, r);
        state.emplace_back(position, double2{}, velocity, acceleration, 0.0f, 0.0f, fixed);
        return masses.size() - 1;
    }

    static size_t add_mass(double m, double r, double e, double2 position, double2 velocity, double2 acceleration, bool fixed, std::vector<mass>& masses, std::vector<mass_state>& state)
    {
        masses.emplace_back(m, r, e);
        state.emplace_back(position, double2{}, velocity, acceleration, 0.0f, 0.0f, fixed);
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
            s.force_ = double2();
            s.acceleration_ = double2();
        }
    }

    void update_broken_springs_and_dampers(const std::vector<mass_state>& mass_state, std::vector<spring_state>& spring_state, std::vector<damper_state>& damper_state)
    {
        for (size_t i = 0; i < model_.springs().size(); ++i)
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
        for (int i = 0; i < count_masses; ++i)
        {
            const auto& m1 = model_.masses()[i];
            const auto& position1 = state[i].position_;

            for (int j = i + 1; j < count_masses; ++j)
            {
                const auto& m2 = model_.masses()[j];
                if (m1.m() + m2.m() < min_gravitational_mass_)
                    continue;


                const auto& position2 = state[j].position_;

                //Unit vector m1->m2
                const auto& u = (position2 - position1).unit_vector();
                //if (u.is_near_zero()) //Don't apply zero distance force
                  //  continue;

                //Distance m1->m2
                const auto distance = position1.distance(position2);

                //Calculate scalar gravitational force acting on m1 due to m2 and apply to unit vector
                const auto scalar_force = (constants::G * m1.m()) * (m2.m() / (distance * distance));
                const auto sfu = scalar_force * u;
                state[i].force_ += sfu;

                //m2 has the opposite force
                state[j].force_ -= sfu;
            }
        }
    }

    void update_spring_force(std::vector<mass_state>& state, const std::vector<spring_state>& spring_states) const
    {
        const auto& count_springs = model_.springs().size();

        for (int i = 0; i < count_springs; ++i)
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

    void update_damper_force(std::vector <mass_state>& state, const std::vector<damper_state>& damper_states) const
    {
        const auto& count_dampers = model_.dampers().size();

        for (int i = 0; i < count_dampers; ++i)
        {
            if (damper_states[i].broken_)
                continue;

            const auto& damper = model_.dampers()[i];
            const auto id_mass1 = damper.id_mass1();
            const auto id_mass2 = damper.id_mass2();
            const auto& A  = state[id_mass1].position_;
            const auto& B  = state[id_mass2].position_;
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

        for (int i = 0; i < count_masses; ++i)
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
        for (int i = 0; i < state.size(); ++i)
        {
            auto& s = state[i];
            if (s.position_.y() + model_.masses()[i].r() > ymax())
            {
                s.velocity_ = double2(0.8 * s.velocity_.x(), -0.92f * fabs(s.velocity_.y()));
            }
        }
    }

    void update_collisions(std::vector<mass_state>& state)
    {
        const auto& masses = model_.masses();
        for (int i = 0; i < state.size(); ++i)
        {
            auto& state1      = state[i];
            const auto& mass1 = masses[i];
            const auto& p1    = state1.position_;

            for (int j = i + 1; j < state.size(); ++j)
            {
                auto& state2   = state[j];
                const auto& p2 = state2.position_;

                const auto& p_diff = p2 - p1;

                if (abs(p_diff.x()) < 1.0E-12 && abs(p_diff.y()) < 1.0E-12)
                    continue;

                const auto& mass2 = masses[j];
                if (p_diff.sum_squares() > (mass1.r() + mass1.r()) * (mass2.r() + mass2.r()))
                    continue;

                const auto m1 = mass1.m();
                const auto m2 = mass2.m();
                const auto M  = m1 + m2;

                const auto distance = p_diff.modulus();

                const auto e = (distance - mass1.r() - mass2.r());

                const auto& u  = p_diff.unit_vector();
                const auto& du = e * u;

                const auto dr    =  m2 / M;
                state1.position_ += dr * du;
                state2.position_ -= (1 - dr) * du;

                const auto elastic_coeff = mass1.e() * mass2.e();
                const auto ideal_distance = mass1.r() + mass2.r();
                const auto ideal_distance_squared = ideal_distance * ideal_distance;

                const auto v1  = state1.velocity_;
                const auto v2  = state2.velocity_;

                state1.velocity_ = v1 - 2.0 * elastic_coeff * (m2 / M) * dot_product(v1 - v2, p1 - p2) * (p1 - p2) / ideal_distance_squared;
                state2.velocity_ = v2 - 2.0 * elastic_coeff * (m1 / M) * dot_product(v2 - v1, p2 - p1) * (p2 - p1) / ideal_distance_squared;
            }
        }
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

