#pragma once

#include "model_system.h"
#include <cmath>
#include <vector>

struct mass_state
{
    mass_state() = default;
    mass_state(const double2& position, const double2& force, const double2& velocity, const double2& acceleration, float angular_velocity, float angular_position, bool fixed)
        : position_(position), force_(force), velocity_(velocity), acceleration_(acceleration), angular_velocity_(angular_velocity), angular_position_(angular_position), fixed_(fixed)
    {}

    double2 position_;
    double2 force_;
    double2 velocity_;
    double2 acceleration_;
    
    float  angular_velocity_;
    float  angular_position_;

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
    simulation(const model_system& model) : model_(model) {}

    static size_t add_mass(float m, float r, double2 position, double2 velocity, double2 acceleration, bool fixed, std::vector<mass>& masses, std::vector<mass_state>& state)
    {
        masses.emplace_back(m, r);
        state.emplace_back(position, double2{}, velocity, acceleration, 0.0f, 0.0f, fixed);
        return masses.size() - 1;
    }

    static size_t add_spring(size_t id_mass1, size_t id_mass2, bool working, float k, float l0, std::vector<spring>& springs, std::vector<spring_state>& state)
    {
        springs.emplace_back(id_mass1, id_mass2, k, l0);
        state.emplace_back(false);
        return springs.size() - 1;
    }

    static size_t add_damper(size_t id_mass1, size_t id_mass2, float k, float l0, const spring& spring, std::vector<damper>& dampers, std::vector<damper_state>& state)
    {
        dampers.emplace_back(id_mass1, id_mass2, spring);
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

            for (size_t j = 0; j < count_masses; ++j)
            {
                if (j == i)
                    continue;
                const auto& m2 = model_.masses()[j];
                const auto& position2 = state[j].position_;

                //Unit vector m1->m2
                const auto& u = (position2 - position1).unit_vector();

                //Distance m1->m2
                const auto distance = position1.distance(position2);

                //Calculate scalar gravitational force acting on m1 due to m2 and apply to unit vector
                const auto scalar_force = distance > constants::minimum_distance ?
                    constants::G * m1.m() * m2.m() / (distance * distance)
                    : 0.0f;
                state[i].force_ += scalar_force * u;
            }
        }
    }

    void update_spring_force(std::vector<mass_state>& state, const std::vector<spring_state>& spring_states) const
    {
        const auto& count_springs = model_.springs().size();

#pragma omp parallel for
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
            const auto speed = velocity1.distance(velocity2);

            const auto scalar_force = spring.force(length, speed);

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

    void update_spatial(std::vector<mass_state>& state, float dt) const
    {
        const auto count_masses = model_.masses().size();

#pragma omp parallel for
        for (int i = 0; i < count_masses; ++i)
        {
            const auto& mass = model_.masses()[i];
            state[i].acceleration_ = state[i].force_ / mass.m();
            
            if (!state[i].fixed_)
            {
                state[i].velocity_ += state[i].acceleration_ * dt;
                state[i].position_ += state[i].velocity_ * dt;
            }
        }

        for (const auto& spring : model_.springs())
        {
            const auto id_mass1 = spring.id_mass1();
            const auto id_mass2 = spring.id_mass2();

            const auto& m1 = model_.masses()[id_mass1];
            const auto& m2 = model_.masses()[id_mass2];

            auto& a = state[id_mass1].position_;
            auto& b = state[id_mass2].position_;

            const auto& length = (a - b).modulus();
            if (length < constants::minimum_distance)
                continue;

            if (length < m1.r() + m2.r()) //collision
            {
                const auto u = (a - b).unit_vector();
                a = a + 0.5 * u;
                b = b - 0.5 * u;
                
                const auto& idist = m1.r() + m2.r();
                const auto& length2 = (a - b).modulus();
                printf("");
            }

        }

    }

    void update_floor(float limit, std::vector<mass_state>& state) const
    {
        for (int i = 0; i < state.size(); ++i)
        {
            auto& s = state[i];
            if (s.position_.y() + model_.masses()[i].r() > limit)
            {
                s.velocity_ = double2(0.5f * s.velocity_.x(), -0.7f * fabs(s.velocity_.y()));
            }
        }
    }

    const auto& model() const
    {
        return model_;
    }

private:
    model_system    model_; //the system being simulated
};