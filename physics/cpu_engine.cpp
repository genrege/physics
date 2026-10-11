#include "cpu_engine.h"
#include "physics_math.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
    template<typename S>
    void assign_flags(std::vector<S>& out, const std::vector<char>& flags)
    {
        if (out.size() != flags.size())
        {
            out.clear();
            for (size_t i = 0; i < flags.size(); ++i)
                out.emplace_back(flags[i] != 0);
        }
        else
        {
            for (size_t i = 0; i < flags.size(); ++i)
                out[i].broken_ = flags[i] != 0;
        }
    }
}

void cpu_engine::upload(const simulation& sim, const std::vector<mass_state>& masses, const std::vector<spring_state>& springs, const std::vector<damper_state>& dampers)
{
    sim_ = std::make_unique<simulation>(sim);

    const auto n = masses.size();
    position_.resize(n);
    velocity_.resize(n);
    acceleration_.resize(n);
    force_gravity_.assign(n, vector2());
    force_spring_.assign(n, vector2());
    force_damper_.assign(n, vector2());
    force_total_.assign(n, vector2());
    fixed_.resize(n);
    angular_velocity_.resize(n);
    angular_position_.resize(n);

    for (size_t i = 0; i < n; ++i)
    {
        position_[i] = masses[i].position_;
        velocity_[i] = masses[i].velocity_;
        acceleration_[i] = masses[i].acceleration_;
        force_total_[i] = masses[i].force_;
        fixed_[i] = masses[i].fixed_ ? 1 : 0;
        angular_velocity_[i] = masses[i].angular_velocity_;
        angular_position_[i] = masses[i].angular_position_;
    }

    spring_broken_.resize(springs.size());
    for (size_t i = 0; i < springs.size(); ++i)
        spring_broken_[i] = springs[i].broken_ ? 1 : 0;

    damper_broken_.resize(dampers.size());
    for (size_t i = 0; i < dampers.size(); ++i)
        damper_broken_[i] = dampers[i].broken_ ? 1 : 0;

    double max_r = 0.0;
    for (const auto& m : sim_->model().masses())
        max_r = std::max(max_r, m.r());
    collision_cell_size_ = std::max(2.0 * max_r, 1e-3);
}

void cpu_engine::download(std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers)
{
    masses.resize(position_.size());
    for (size_t i = 0; i < position_.size(); ++i)
    {
        masses[i] = mass_state(position_[i], force_total_[i], velocity_[i], acceleration_[i], angular_velocity_[i], angular_position_[i], fixed_[i] != 0);
    }
    assign_flags(springs, spring_broken_);
    assign_flags(dampers, damper_broken_);
}

void cpu_engine::download_render(std::vector<mass_state>& masses, std::vector<spring_state>& springs)
{
    masses.resize(position_.size());
    for (size_t i = 0; i < position_.size(); ++i)
    {
        masses[i].position_ = position_[i];
        masses[i].velocity_ = velocity_[i];
        masses[i].fixed_ = fixed_[i] != 0;
    }
    assign_flags(springs, spring_broken_);
}

void cpu_engine::update_broken()
{
    const auto& springs = sim_->model().springs();
    const auto& dampers = sim_->model().dampers();

    for (size_t i = 0; i < springs.size(); ++i)
    {
        if (spring_broken_[i])
            continue;

        const auto& spring = springs[i];
        const auto id_mass1 = spring.id_mass1();
        const auto id_mass2 = spring.id_mass2();
        const auto length = position_[id_mass1].distance(position_[id_mass2]);

        if (length < spring.min_length() || length > spring.max_length())
        {
            spring_broken_[i] = 1;

            for (size_t j = 0; j < dampers.size(); ++j)
            {
                if (dampers[j].id_mass1() == id_mass1 && dampers[j].id_mass2() == id_mass2)
                    damper_broken_[j] = 1;
            }
        }
    }
}

void cpu_engine::compute_gravity()
{
    const auto& masses = sim_->model().masses();
    const auto n = masses.size();
    const auto min_gravitational_mass = sim_->min_gravitational_mass();
    const auto softening = sim_->softening();

    std::fill(force_gravity_.begin(), force_gravity_.end(), vector2());

    // Pair scatter; each mass receives its partners in ascending index order, matching the CUDA gather.
    for (size_t i = 0; i < n; ++i)
    {
        for (size_t j = i + 1; j < n; ++j)
        {
            vector2 f;
            if (!physics_math::gravity_pair(masses[i].m(), masses[j].m(), position_[i], position_[j], min_gravitational_mass, softening, f))
                continue;
            force_gravity_[i] += f;
            force_gravity_[j] -= f;
        }
    }
}

void cpu_engine::compute_springs()
{
    const auto& springs = sim_->model().springs();

    std::fill(force_spring_.begin(), force_spring_.end(), vector2());

    for (size_t i = 0; i < springs.size(); ++i)
    {
        if (spring_broken_[i])
            continue;

        const auto& spring = springs[i];
        const auto f = physics_math::spring_pair_force(position_[spring.id_mass1()], position_[spring.id_mass2()], spring.k(), spring.l0());
        force_spring_[spring.id_mass1()] += f;
        force_spring_[spring.id_mass2()] -= f;
    }
}

void cpu_engine::compute_dampers()
{
    const auto& dampers = sim_->model().dampers();

    std::fill(force_damper_.begin(), force_damper_.end(), vector2());

    for (size_t i = 0; i < dampers.size(); ++i)
    {
        if (damper_broken_[i])
            continue;

        const auto& damper = dampers[i];
        const auto id1 = damper.id_mass1();
        const auto id2 = damper.id_mass2();
        const auto f = physics_math::damper_pair_force(position_[id1], position_[id2], velocity_[id1], velocity_[id2], damper.k());
        force_damper_[id1] += f;
        force_damper_[id2] -= f;
    }
}

void cpu_engine::sum_forces()
{
    const auto& masses = sim_->model().masses();
    for (size_t i = 0; i < masses.size(); ++i)
    {
        force_total_[i] = (force_gravity_[i] + force_spring_[i]) + force_damper_[i];
        acceleration_[i] = force_total_[i] / masses[i].m();
    }
}

void cpu_engine::integrate(double dt)
{
    for (size_t i = 0; i < position_.size(); ++i)
    {
        if (fixed_[i])
            continue;
        velocity_[i] += acceleration_[i] * dt;
        position_[i] += velocity_[i] * dt;
    }
}

void cpu_engine::collisions()
{
    const auto& masses = sim_->model().masses();
    const size_t count = masses.size();
    if (count <= 1)
        return;

    std::unordered_map<long long, std::vector<size_t>> spatial_hash;
    spatial_hash.reserve(count * 2);

    for (size_t i = 0; i < count; ++i)
    {
        const auto cx = physics_math::cell_coordinate(position_[i].x(), collision_cell_size_);
        const auto cy = physics_math::cell_coordinate(position_[i].y(), collision_cell_size_);
        spatial_hash[physics_math::cell_key(cx, cy)].push_back(i);
    }

    collision_dpos_.assign(count, vector2());
    collision_dvel_.assign(count, vector2());

    // Every pair is evaluated from start-of-pass state; each mass sums its contacts in ascending partner
    // index (the order the CUDA engine merges its cells in) and the deltas are applied afterwards.
    std::vector<size_t> candidates;
    for (size_t i = 0; i < count; ++i)
    {
        const auto cx = physics_math::cell_coordinate(position_[i].x(), collision_cell_size_);
        const auto cy = physics_math::cell_coordinate(position_[i].y(), collision_cell_size_);

        candidates.clear();
        for (long long dx_cell = -1; dx_cell <= 1; ++dx_cell)
        {
            for (long long dy_cell = -1; dy_cell <= 1; ++dy_cell)
            {
                const auto it = spatial_hash.find(physics_math::cell_key(cx + dx_cell, cy + dy_cell));
                if (it == spatial_hash.end())
                    continue;

                for (const auto j : it->second)
                {
                    if (j != i)
                        candidates.push_back(j);
                }
            }
        }
        std::sort(candidates.begin(), candidates.end());

        for (const auto j : candidates)
        {
            physics_math::collision_delta d;
            if (i < j)
            {
                if (!physics_math::collision_pair(position_[i], position_[j], velocity_[i], velocity_[j],
                    masses[i].m(), masses[j].m(), masses[i].r(), masses[j].r(), masses[i].e(), masses[j].e(), d))
                    continue;
                collision_dpos_[i] += d.dpos_1;
                collision_dvel_[i] += d.dvel_1;
            }
            else
            {
                if (!physics_math::collision_pair(position_[j], position_[i], velocity_[j], velocity_[i],
                    masses[j].m(), masses[i].m(), masses[j].r(), masses[i].r(), masses[j].e(), masses[i].e(), d))
                    continue;
                collision_dpos_[i] += d.dpos_2;
                collision_dvel_[i] += d.dvel_2;
            }
        }
    }

    for (size_t i = 0; i < count; ++i)
    {
        position_[i] += collision_dpos_[i];
        velocity_[i] += collision_dvel_[i];
    }
}

void cpu_engine::walls()
{
    const auto& masses = sim_->model().masses();
    const auto& w = sim_->walls();
    for (size_t i = 0; i < position_.size(); ++i)
    {
        velocity_[i] = physics_math::wall_bounce(position_[i], velocity_[i], masses[i].r(),
            sim_->xmin(), sim_->xmax(), sim_->ymin(), sim_->ymax(), w.left, w.right, w.top, w.bottom);
    }
}

void cpu_engine::read_forces(force_generator generator, std::vector<vector2>& out)
{
    switch (generator)
    {
    case force_generator::gravity: out = force_gravity_; break;
    case force_generator::spring:  out = force_spring_; break;
    case force_generator::damper:  out = force_damper_; break;
    case force_generator::total:   out = force_total_; break;
    }
}
