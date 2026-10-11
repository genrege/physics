#pragma once

#include "CppUnitTest.h"
#include "physics_engine.h"
#include "simulation.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace physics_test
{
    using namespace Microsoft::VisualStudio::CppUnitTestFramework;

    struct scenario
    {
        std::shared_ptr<simulation> sim;
        std::vector<mass_state> masses;
        std::vector<spring_state> springs;
        std::vector<damper_state> dampers;
    };

    struct scenario_options
    {
        bool with_springs = true;
        bool dense = false;          // pack masses so many overlap (collisions)
        bool all_walls = false;      // bounce off all four edges, with the walls inside the cloud of masses
        double extent = 500.0;
        double softening = 0.0;
    };

    // Random system with fixed masses, light masses below the gravity threshold, springs that may already be
    // outside their break range, dampers with and without springs, and masses below the floor line.
    inline scenario make_scenario(size_t n, unsigned seed, const scenario_options& options = {})
    {
        std::mt19937 rng(seed);
        auto uniform = [&](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };

        const double extent = options.dense ? std::max(20.0, std::sqrt(double(n)) * 8.0) : options.extent;

        std::vector<mass> masses;
        std::vector<spring> springs;
        std::vector<damper> dampers;
        scenario s;

        for (size_t i = 0; i < n; ++i)
        {
            const bool light = (rng() % 4) == 0;
            const double m = light ? uniform(1.0, 100.0) : uniform(5.0e3, 5.0e5);
            const double r = options.dense ? uniform(3.0, 8.0) : uniform(2.0, 20.0);
            const double e = uniform(0.5, 1.0);
            const bool fixed = (rng() % 10) == 0;
            simulation::add_mass(m, r, e, vector2(uniform(-extent, extent), uniform(-extent, extent)),
                vector2(uniform(-10, 10), uniform(-10, 10)), vector2(), fixed, masses, s.masses);
        }

        if (options.with_springs && n > 1)
        {
            for (size_t i = 0; i + 1 < n; ++i)
            {
                const size_t a = i;
                const size_t b = (rng() % 3 == 0) ? size_t(rng() % n) : i + 1;
                if (a == b)
                    continue;
                const double length = s.masses[a].position_.distance(s.masses[b].position_);
                // Some springs start outside [0.5, 1.5] * l0 so they break on the first check.
                const double l0 = length * uniform(0.6, 1.9);
                const auto idx = simulation::add_spring(a, b, true, uniform(1.0, 50.0), l0, springs, s.springs);

                if (rng() % 2 == 0)
                    simulation::add_damper(a, b, springs[idx], dampers, s.dampers);
            }
            for (size_t i = 0; i + 2 < n && i < 20; ++i)
                simulation::add_damper(i, i + 2, uniform(0.1, 2.0), 1.0, dampers, s.dampers);

            // Duplicate damper on an existing spring pair to exercise damper coupling with more than one match.
            if (!springs.empty())
                simulation::add_damper(springs[0].id_mass1(), springs[0].id_mass2(), 0.3, 1.0, dampers, s.dampers);
        }

        wall_flags walls;
        double wall_extent = extent;
        if (options.all_walls)
        {
            walls = { true, true, true, true };
            wall_extent = extent * 0.5;
        }
        s.sim = std::make_shared<simulation>(model_system(masses, springs, dampers), -wall_extent, wall_extent, -wall_extent, options.all_walls ? wall_extent : extent * 0.5, 0.01, 1, 10000.0, walls, 0.0, options.softening);
        return s;
    }

    // rel == 0 requires bit equality; otherwise the error is bounded relative to the largest magnitude in the array,
    // since chaotic amplification makes per-element relative error meaningless near zero crossings.
    inline bool close(double a, double b, double rel, double abs_tol)
    {
        if (std::isnan(a) && std::isnan(b))
            return true;
        if (a == b)
            return true;
        return std::fabs(a - b) <= rel * std::max(std::fabs(a), std::fabs(b)) + abs_tol;
    }

    inline void expect_vectors(const char* what, const std::vector<vector2>& expected, const std::vector<vector2>& actual, double rel)
    {
        std::ostringstream os;
        if (expected.size() != actual.size())
        {
            os << what << ": size " << expected.size() << " vs " << actual.size();
            Assert::Fail(std::wstring(os.str().begin(), os.str().end()).c_str());
        }
        double scale = 0.0;
        for (const auto& v : expected)
        {
            if (std::isfinite(v.x())) scale = std::max(scale, std::fabs(v.x()));
            if (std::isfinite(v.y())) scale = std::max(scale, std::fabs(v.y()));
        }
        const double abs_tol = rel * scale;

        for (size_t i = 0; i < expected.size(); ++i)
        {
            if (!close(expected[i].x(), actual[i].x(), rel, abs_tol) || !close(expected[i].y(), actual[i].y(), rel, abs_tol))
            {
                os << what << "[" << i << "]: cpu (" << expected[i].x() << ", " << expected[i].y() << ") vs cuda (" << actual[i].x() << ", " << actual[i].y() << ")"
                   << std::setprecision(17) << " dx=" << (expected[i].x() - actual[i].x()) << " dy=" << (expected[i].y() - actual[i].y());
                const auto text = os.str();
                Assert::Fail(std::wstring(text.begin(), text.end()).c_str());
            }
        }
    }

    inline void expect_states(const std::vector<mass_state>& expected, const std::vector<mass_state>& actual,
        const std::vector<spring_state>& expected_springs, const std::vector<spring_state>& actual_springs,
        const std::vector<damper_state>& expected_dampers, const std::vector<damper_state>& actual_dampers, double rel)
    {
        Assert::AreEqual(expected.size(), actual.size());
        auto pick = [](const std::vector<mass_state>& v, vector2 mass_state::* member) {
            std::vector<vector2> out;
            for (const auto& s : v) out.push_back(s.*member);
            return out;
            };
        expect_vectors("position", pick(expected, &mass_state::position_), pick(actual, &mass_state::position_), rel);
        expect_vectors("velocity", pick(expected, &mass_state::velocity_), pick(actual, &mass_state::velocity_), rel);
        expect_vectors("acceleration", pick(expected, &mass_state::acceleration_), pick(actual, &mass_state::acceleration_), rel);
        expect_vectors("force", pick(expected, &mass_state::force_), pick(actual, &mass_state::force_), rel);

        Assert::AreEqual(expected_springs.size(), actual_springs.size());
        for (size_t i = 0; i < expected_springs.size(); ++i)
            Assert::AreEqual(expected_springs[i].broken_, actual_springs[i].broken_, L"spring broken flag");

        Assert::AreEqual(expected_dampers.size(), actual_dampers.size());
        for (size_t i = 0; i < expected_dampers.size(); ++i)
            Assert::AreEqual(expected_dampers[i].broken_, actual_dampers[i].broken_, L"damper broken flag");
    }

    // Creates a CUDA engine or fails the test: a regression run without a usable GPU must not pass silently.
    inline std::unique_ptr<physics_engine> require_cuda()
    {
        std::string error;
        auto engine = make_cuda_engine(error);
        if (!engine)
        {
            const std::wstring message = L"CUDA unavailable: " + std::wstring(error.begin(), error.end());
            Assert::Fail(message.c_str());
        }
        return engine;
    }

    // Runs the same stage sequence on both engines from identical state; compares the complete state.
    template<typename StageFn>
    void compare_engines(const scenario& s, double rel, StageFn&& stages)
    {
        auto cpu = make_cpu_engine();
        auto cuda = require_cuda();
        cpu->upload(*s.sim, s.masses, s.springs, s.dampers);
        cuda->upload(*s.sim, s.masses, s.springs, s.dampers);

        stages(*cpu);
        stages(*cuda);
        cpu->synchronize();
        cuda->synchronize();

        std::vector<mass_state> cpu_masses, cuda_masses;
        std::vector<spring_state> cpu_springs = s.springs, cuda_springs = s.springs;
        std::vector<damper_state> cpu_dampers = s.dampers, cuda_dampers = s.dampers;
        cpu->download(cpu_masses, cpu_springs, cpu_dampers);
        cuda->download(cuda_masses, cuda_springs, cuda_dampers);
        expect_states(cpu_masses, cuda_masses, cpu_springs, cuda_springs, cpu_dampers, cuda_dampers, rel);
    }

    inline void expect_generator(const scenario& s, force_generator generator, const char* what, double rel)
    {
        auto cpu = make_cpu_engine();
        auto cuda = require_cuda();
        cpu->upload(*s.sim, s.masses, s.springs, s.dampers);
        cuda->upload(*s.sim, s.masses, s.springs, s.dampers);

        auto run = [&](physics_engine& e) {
            e.update_broken();
            e.compute_gravity();
            e.compute_springs();
            e.compute_dampers();
            e.sum_forces();
            e.synchronize();
            };
        run(*cpu);
        run(*cuda);

        std::vector<vector2> expected, actual;
        cpu->read_forces(generator, expected);
        cuda->read_forces(generator, actual);
        expect_vectors(what, expected, actual, rel);
    }
}
