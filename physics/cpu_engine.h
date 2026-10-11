#pragma once

#include "physics_engine.h"

#include <memory>

class cpu_engine : public physics_engine
{
public:
    const char* name() const override { return "CPU"; }

    void upload(const simulation& sim, const std::vector<mass_state>& masses, const std::vector<spring_state>& springs, const std::vector<damper_state>& dampers) override;
    void download(std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers) override;
    void download_render(std::vector<mass_state>& masses, std::vector<spring_state>& springs) override;

    void update_broken() override;
    void compute_gravity() override;
    void compute_springs() override;
    void compute_dampers() override;
    void sum_forces() override;
    void integrate(double dt) override;
    void collisions() override;
    void walls() override;

    void read_forces(force_generator generator, std::vector<vector2>& out) override;

private:
    std::unique_ptr<simulation> sim_;

    // Separate arrays rather than an array of structs, mirroring the device layout.
    std::vector<vector2> position_;
    std::vector<vector2> velocity_;
    std::vector<vector2> acceleration_;
    std::vector<vector2> force_gravity_;
    std::vector<vector2> force_spring_;
    std::vector<vector2> force_damper_;
    std::vector<vector2> force_total_;
    std::vector<char> fixed_;
    std::vector<double> angular_velocity_;
    std::vector<double> angular_position_;

    std::vector<char> spring_broken_;
    std::vector<char> damper_broken_;

    std::vector<vector2> collision_dpos_;
    std::vector<vector2> collision_dvel_;
    double collision_cell_size_ = 1.0;
};
