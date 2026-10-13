#include "test_support.h"
#include "model_files.h"

#include <cstdlib>
#include <functional>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace physics_test;

namespace
{
    const size_t sizes[] = { 2, 5, 255, 256, 257, 1000 };

    void for_each_size(const std::function<void(const scenario&)>& fn, const scenario_options& options = {})
    {
        for (const auto n : sizes)
            fn(make_scenario(n, 1000u + unsigned(n), options));
    }
}

TEST_CLASS(cuda_stage_regression)
{
public:
    TEST_METHOD(update_broken_matches_cpu_and_couples_dampers)
    {
        for_each_size([](const scenario& s) {
            compare_engines(s, 0.0, [](physics_engine& e) { e.update_broken(); });
            });
    }

    TEST_METHOD(broken_flags_are_sticky)
    {
        for_each_size([](const scenario& s) {
            compare_engines(s, 0.0, [](physics_engine& e) { e.update_broken(); e.integrate(100.0); e.update_broken(); });
            });
    }

    TEST_METHOD(gravity_force_matches_cpu)
    {
        for_each_size([](const scenario& s) { expect_generator(s, force_generator::gravity, "gravity", 0.0); });
    }

    TEST_METHOD(softened_gravity_force_matches_cpu)
    {
        scenario_options options;
        options.softening = 25.0;
        for_each_size([](const scenario& s) { expect_generator(s, force_generator::gravity, "softened gravity", 0.0); }, options);
    }

    TEST_METHOD(spring_force_matches_cpu)
    {
        for_each_size([](const scenario& s) { expect_generator(s, force_generator::spring, "spring", 0.0); });
    }

    TEST_METHOD(damper_force_matches_cpu)
    {
        for_each_size([](const scenario& s) { expect_generator(s, force_generator::damper, "damper", 0.0); });
    }

    TEST_METHOD(summed_force_and_acceleration_match_cpu)
    {
        for_each_size([](const scenario& s) {
            compare_engines(s, 0.0, [](physics_engine& e) {
                e.update_broken(); e.compute_gravity(); e.compute_springs(); e.compute_dampers(); e.sum_forces();
                });
            });
    }

    TEST_METHOD(integrate_matches_cpu_including_fixed_masses)
    {
        for_each_size([](const scenario& s) {
            compare_engines(s, 0.0, [&](physics_engine& e) {
                e.update_broken(); e.compute_gravity(); e.compute_springs(); e.compute_dampers(); e.sum_forces();
                e.integrate(s.sim->dt());
                });
            });
    }

    TEST_METHOD(walls_match_cpu_for_the_default_floor)
    {
        for_each_size([](const scenario& s) {
            compare_engines(s, 0.0, [](physics_engine& e) { e.walls(); });
            });
    }

    TEST_METHOD(walls_match_cpu_on_all_four_edges)
    {
        scenario_options options;
        options.all_walls = true;
        for_each_size([](const scenario& s) {
            compare_engines(s, 0.0, [](physics_engine& e) { e.walls(); });
            }, options);
    }

    TEST_METHOD(walls_hold_masses_in_over_many_steps_on_both_engines)
    {
        scenario_options options;
        options.all_walls = true;
        const auto s = make_scenario(300, 17u, options);
        compare_engines(s, 0.0, [&](physics_engine& e) { e.step(100, s.sim->dt()); });
    }

    TEST_METHOD(collisions_match_cpu_sparse)
    {
        for_each_size([](const scenario& s) {
            compare_engines(s, 0.0, [](physics_engine& e) { e.collisions(); });
            });
    }

    TEST_METHOD(collisions_match_cpu_dense)
    {
        scenario_options options;
        options.dense = true;
        options.with_springs = false;
        for_each_size([](const scenario& s) {
            compare_engines(s, 0.0, [](physics_engine& e) { e.collisions(); });
            }, options);
    }

    TEST_METHOD(collisions_handle_cell_boundaries_negative_cells_and_coincident_masses)
    {
        std::vector<mass> masses;
        scenario s;
        // Cell size is 2 * max radius = 20. Pairs straddle cell borders, sit in negative cells, or coincide exactly.
        const double r = 10.0;
        const vector2 positions[] = { {19.9, 0.0}, {20.1, 0.0}, {-20.1, -20.1}, {-19.9, -19.9}, {-0.05, 40.0}, {0.05, 40.0}, {100.0, 100.0}, {100.0, 100.0}, {-300.0, 5.0}, {-290.5, 5.0} };
        for (const auto& p : positions)
            simulation::add_mass(2.0, r, 0.9, p, vector2(1.0, -2.0), vector2(), false, masses, s.masses);
        s.sim = std::make_shared<simulation>(model_system(masses, {}, {}), -1000, 1000, -1000, 1000, 0.01, 1);

        compare_engines(s, 0.0, [](physics_engine& e) { e.collisions(); });
    }
};

TEST_CLASS(cuda_full_step_regression)
{
public:
    TEST_METHOD(random_system_multi_step_matches_cpu)
    {
        for (const size_t steps : { size_t(1), size_t(10), size_t(100) })
        {
            const auto s = make_scenario(300, 7u);
            compare_engines(s, 0.0, [&](physics_engine& e) { e.step(steps, s.sim->dt()); });
        }
    }

    TEST_METHOD(example_simulations_match_cpu)
    {
        for (int number = 1; number <= 9; ++number)
        {
            // Simulation 4 is 3500 bodies; one step already exercises every stage.
            const size_t max_steps = number == 4 ? 1 : 100;
            for (const size_t steps : { size_t(1), size_t(10), size_t(100) })
            {
                if (steps > max_steps)
                    continue;
                Logger::WriteMessage(("simulation " + std::to_string(number) + ", steps " + std::to_string(steps) + "\n").c_str());
                const auto model = load_model(number);
                scenario s;
                s.sim = model.sim;
                s.masses = model.masses;
                s.springs = model.springs;
                s.dampers = model.dampers;
                compare_engines(s, 0.0, [&](physics_engine& e) { e.step(steps, s.sim->dt()); });
            }
        }
    }

    TEST_METHOD(solar_system_matches_cpu_over_many_steps)
    {
        for (const auto* file : { "solar_system.xml", "solar_system_asteroids.xml" })
        {
            const auto model = load_model_file(models_dir() / file);
            scenario s;
            s.sim = model.sim;
            s.masses = model.masses;
            s.springs = model.springs;
            s.dampers = model.dampers;
            compare_engines(s, 0.0, [&](physics_engine& e) { e.step(2000, s.sim->dt()); });
        }
    }

    TEST_METHOD(every_cuda_device_matches_cpu)
    {
        const auto names = cuda_device_names();
        if (names.empty())
            Assert::Fail(L"no CUDA devices");

        const auto s = make_scenario(500, 11u);
        for (size_t d = 0; d < names.size(); ++d)
        {
            _putenv_s("PHYSICS_CUDA_DEVICE", std::to_string(d).c_str());
            compare_engines(s, 0.0, [&](physics_engine& e) { e.step(10, s.sim->dt()); });
        }
        _putenv_s("PHYSICS_CUDA_DEVICE", "");
    }

    TEST_METHOD(upload_download_round_trip_preserves_state)
    {
        const auto s = make_scenario(257, 3u);
        for (int pass = 0; pass < 2; ++pass)
        {
            std::unique_ptr<physics_engine> engine = pass == 0 ? make_cpu_engine() : require_cuda();
            engine->upload(*s.sim, s.masses, s.springs, s.dampers);

            std::vector<mass_state> masses;
            std::vector<spring_state> springs = s.springs;
            std::vector<damper_state> dampers = s.dampers;
            engine->download(masses, springs, dampers);
            expect_states(s.masses, masses, s.springs, springs, s.dampers, dampers, 0.0);
        }
    }

    TEST_METHOD(switching_engines_mid_run_matches_continuous_run)
    {
        const auto s = make_scenario(200, 5u);
        const auto dt = s.sim->dt();

        auto reference = make_cpu_engine();
        reference->upload(*s.sim, s.masses, s.springs, s.dampers);
        reference->step(20, dt);

        auto cpu = make_cpu_engine();
        cpu->upload(*s.sim, s.masses, s.springs, s.dampers);
        cpu->step(10, dt);

        std::vector<mass_state> masses;
        std::vector<spring_state> springs = s.springs;
        std::vector<damper_state> dampers = s.dampers;
        cpu->download(masses, springs, dampers);

        auto cuda = require_cuda();
        cuda->upload(*s.sim, masses, springs, dampers);
        cuda->step(10, dt);

        std::vector<mass_state> expected, actual;
        std::vector<spring_state> expected_springs = s.springs, actual_springs = s.springs;
        std::vector<damper_state> expected_dampers = s.dampers, actual_dampers = s.dampers;
        reference->download(expected, expected_springs, expected_dampers);
        cuda->download(actual, actual_springs, actual_dampers);
        expect_states(expected, actual, expected_springs, actual_springs, expected_dampers, actual_dampers, 0.0);
    }

    TEST_METHOD(factory_always_returns_a_usable_engine)
    {
        std::string error;
        auto engine = make_engine(engine_kind::cuda, error);
        Assert::IsNotNull(engine.get());
        if (!error.empty())
            Assert::AreEqual("CPU", engine->name());
    }
};
