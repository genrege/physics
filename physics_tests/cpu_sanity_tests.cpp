#include "test_support.h"
#include "physics_math.h"

#include <cmath>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace physics_test;

namespace
{
    scenario two_masses(double m1, double m2, double r, const vector2& p1, const vector2& p2, const vector2& v1, const vector2& v2, double e = 1.0)
    {
        std::vector<mass> masses;
        scenario s;
        simulation::add_mass(m1, r, e, p1, v1, vector2(), false, masses, s.masses);
        simulation::add_mass(m2, r, e, p2, v2, vector2(), false, masses, s.masses);
        s.sim = std::make_shared<simulation>(model_system(masses, {}, {}), -1000, 1000, -1000, 1000, 0.01, 1);
        return s;
    }

    void download(physics_engine& engine, const scenario& s, std::vector<mass_state>& masses)
    {
        auto springs = s.springs;
        auto dampers = s.dampers;
        engine.download(masses, springs, dampers);
    }

    // One unit-radius mass in a +-1000 world; returns its velocity after the walls stage.
    vector2 velocity_after_walls(vector2 position, vector2 velocity, wall_flags walls)
    {
        std::vector<mass> masses;
        scenario s;
        simulation::add_mass(1.0, 1.0, position, velocity, vector2(), false, masses, s.masses);
        s.sim = std::make_shared<simulation>(model_system(masses, {}, {}), -1000, 1000, -1000, 1000, 0.01, 1, 10000.0, walls);

        auto engine = make_cpu_engine();
        engine->upload(*s.sim, s.masses, s.springs, s.dampers);
        engine->walls();

        std::vector<mass_state> after;
        download(*engine, s, after);
        return after[0].velocity_;
    }
}

TEST_CLASS(cpu_physics_sanity)
{
public:
    TEST_METHOD(collision_conserves_momentum)
    {
        const auto s = two_masses(3.0, 7.0, 1.0, vector2(0, 0), vector2(1.5, 0), vector2(4, 1), vector2(-2, 0.5));
        auto engine = make_cpu_engine();
        engine->upload(*s.sim, s.masses, s.springs, s.dampers);
        engine->collisions();

        std::vector<mass_state> after;
        download(*engine, s, after);

        const auto px = 3.0 * after[0].velocity_.x() + 7.0 * after[1].velocity_.x();
        const auto py = 3.0 * after[0].velocity_.y() + 7.0 * after[1].velocity_.y();
        Assert::AreEqual(3.0 * 4 + 7.0 * -2, px, 1e-9);
        Assert::AreEqual(3.0 * 1 + 7.0 * 0.5, py, 1e-9);
    }

    TEST_METHOD(collision_leaves_no_residual_overlap)
    {
        const auto s = two_masses(3.0, 7.0, 1.0, vector2(0, 0), vector2(1.5, 0.4), vector2(), vector2());
        auto engine = make_cpu_engine();
        engine->upload(*s.sim, s.masses, s.springs, s.dampers);
        engine->collisions();

        std::vector<mass_state> after;
        download(*engine, s, after);
        Assert::AreEqual(2.0, after[0].position_.distance(after[1].position_), 1e-9);
    }

    TEST_METHOD(separated_masses_do_not_collide)
    {
        const auto s = two_masses(3.0, 7.0, 1.0, vector2(0, 0), vector2(5, 0), vector2(1, 0), vector2(-1, 0));
        auto engine = make_cpu_engine();
        engine->upload(*s.sim, s.masses, s.springs, s.dampers);
        engine->collisions();

        std::vector<mass_state> after;
        download(*engine, s, after);
        Assert::AreEqual(1.0, after[0].velocity_.x());
        Assert::AreEqual(5.0, after[1].position_.x());
    }

    TEST_METHOD(gravity_follows_newtons_law)
    {
        const double m1 = 1.0e12, m2 = 2.0e12, d = 50.0;
        const auto s = two_masses(m1, m2, 1.0, vector2(0, 0), vector2(d, 0), vector2(), vector2());
        auto engine = make_cpu_engine();
        engine->upload(*s.sim, s.masses, s.springs, s.dampers);
        engine->compute_gravity();

        std::vector<vector2> f;
        engine->read_forces(force_generator::gravity, f);
        const double expected = constants::G * m1 * m2 / (d * d);
        Assert::AreEqual(expected, f[0].x(), expected * 1e-12);
        Assert::AreEqual(-expected, f[1].x(), expected * 1e-12);
    }

    TEST_METHOD(softened_gravity_follows_the_plummer_law_and_stays_finite)
    {
        const double m1 = 1.0e12, m2 = 2.0e12, d = 50.0, eps = 30.0;
        vector2 f;
        Assert::IsTrue(physics_math::gravity_pair(m1, m2, vector2(0, 0), vector2(d, 0), 0.0, eps, f));
        const double expected = constants::G * m1 * m2 * d / std::pow(d * d + eps * eps, 1.5);
        Assert::AreEqual(expected, f.x(), expected * 1e-12);
        Assert::AreEqual(0.0, f.y());

        // Zero softening is exactly the old inverse-square result, and coincident masses give no force.
        vector2 plain;
        Assert::IsTrue(physics_math::gravity_pair(m1, m2, vector2(0, 0), vector2(d, 0), 0.0, 0.0, plain));
        Assert::AreEqual(constants::G * m1 * m2 / (d * d), plain.x(), constants::G * m1 * m2 / (d * d) * 1e-12);
        vector2 coincident(1.0, 1.0);
        Assert::IsTrue(physics_math::gravity_pair(m1, m2, vector2(3, 4), vector2(3, 4), 0.0, eps, coincident));
        Assert::AreEqual(0.0, coincident.x());
        Assert::AreEqual(0.0, coincident.y());
    }

    TEST_METHOD(bottom_wall_is_on_by_default_and_applies_the_floor_factors)
    {
        const auto v = velocity_after_walls(vector2(0, 999.5), vector2(10, 5), wall_flags{});
        Assert::AreEqual(8.0, v.x(), 1e-12);
        Assert::AreEqual(-0.92f * 5.0, v.y(), 1e-12);
    }

    TEST_METHOD(top_wall_bounces_downwards_when_enabled)
    {
        const auto v = velocity_after_walls(vector2(0, -999.5), vector2(10, -5), wall_flags{ false, false, true, true });
        Assert::AreEqual(8.0, v.x(), 1e-12);
        Assert::AreEqual(0.92f * 5.0, v.y(), 1e-12);
    }

    TEST_METHOD(left_wall_bounces_rightwards_when_enabled)
    {
        const auto v = velocity_after_walls(vector2(-999.5, 0), vector2(-10, 5), wall_flags{ true, false, false, true });
        Assert::AreEqual(0.92f * 10.0, v.x(), 1e-12);
        Assert::AreEqual(4.0, v.y(), 1e-12);
    }

    TEST_METHOD(right_wall_bounces_leftwards_when_enabled)
    {
        const auto v = velocity_after_walls(vector2(999.5, 0), vector2(10, 5), wall_flags{ false, true, false, true });
        Assert::AreEqual(-0.92f * 10.0, v.x(), 1e-12);
        Assert::AreEqual(4.0, v.y(), 1e-12);
    }

    TEST_METHOD(disabled_walls_do_not_bounce)
    {
        // Only the bottom is on by default, so the other three edges let masses through.
        for (const auto& at : { vector2(0, -999.5), vector2(-999.5, 0), vector2(999.5, 0) })
        {
            const auto v = velocity_after_walls(at, vector2(-10, -5), wall_flags{});
            Assert::AreEqual(-10.0, v.x());
            Assert::AreEqual(-5.0, v.y());
        }
        const auto v = velocity_after_walls(vector2(0, 999.5), vector2(10, 5), wall_flags{ true, true, true, false });
        Assert::AreEqual(10.0, v.x());
        Assert::AreEqual(5.0, v.y());
    }

    TEST_METHOD(a_mass_inside_the_walls_is_left_alone)
    {
        const auto v = velocity_after_walls(vector2(10, 10), vector2(3, 4), wall_flags{ true, true, true, true });
        Assert::AreEqual(3.0, v.x());
        Assert::AreEqual(4.0, v.y());
    }

    TEST_METHOD(a_mass_stays_inside_a_box_with_four_walls)
    {
        std::vector<mass> masses;
        scenario s;
        simulation::add_mass(1.0, 1.0, vector2(0, 0), vector2(37, -23), vector2(), false, masses, s.masses);
        s.sim = std::make_shared<simulation>(model_system(masses, {}, {}), -100, 100, -100, 100, 0.01, 1, 10000.0, wall_flags{ true, true, true, true });

        auto engine = make_cpu_engine();
        engine->upload(*s.sim, s.masses, s.springs, s.dampers);
        engine->step(20000, 0.01);

        std::vector<mass_state> after;
        download(*engine, s, after);
        Assert::IsTrue(std::fabs(after[0].position_.x()) < 110.0 && std::fabs(after[0].position_.y()) < 110.0);
    }

    TEST_METHOD(spring_mass_oscillates_with_expected_period)
    {
        // k = 100, m = 1 => omega = 10, period = 2*pi/10.
        std::vector<mass> masses;
        std::vector<spring> springs;
        scenario s;
        simulation::add_mass(1.0e6, 0.01, vector2(0, 0), vector2(), vector2(), true, masses, s.masses);
        simulation::add_mass(1.0, 0.01, vector2(1.1, 0), vector2(), vector2(), false, masses, s.masses);
        simulation::add_spring(0, 1, true, 100.0, 1.0, springs, s.springs);
        // Gravity threshold excludes the heavy mass from this test.
        s.sim = std::make_shared<simulation>(model_system(masses, springs, {}), -1000, 1000, -1000, 1000, 1e-4, 1, 1e30);

        auto engine = make_cpu_engine();
        engine->upload(*s.sim, s.masses, s.springs, s.dampers);

        const double period = 2.0 * 3.14159265358979323846 / 10.0;
        const auto steps = size_t(std::llround(period / 1e-4));
        engine->step(steps, 1e-4);

        std::vector<mass_state> after;
        download(*engine, s, after);
        Assert::AreEqual(1.1, after[1].position_.x(), 2e-3);
    }

    TEST_METHOD(two_cpu_runs_are_identical)
    {
        const auto s = make_scenario(200, 9u);
        auto a = make_cpu_engine();
        auto b = make_cpu_engine();
        a->upload(*s.sim, s.masses, s.springs, s.dampers);
        b->upload(*s.sim, s.masses, s.springs, s.dampers);
        a->step(25, s.sim->dt());
        b->step(25, s.sim->dt());

        std::vector<mass_state> ma, mb;
        auto sa = s.springs, sb = s.springs;
        auto da = s.dampers, db = s.dampers;
        a->download(ma, sa, da);
        b->download(mb, sb, db);
        expect_states(ma, mb, sa, sb, da, db, 0.0);
    }
};
