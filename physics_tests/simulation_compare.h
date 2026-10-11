#pragma once

#include "test_support.h"

namespace physics_test
{
    struct loaded_simulation
    {
        std::shared_ptr<simulation> sim;
        std::vector<mass_state> masses;
        std::vector<spring_state> springs;
        std::vector<damper_state> dampers;
    };

    // Bit-exact comparison of everything that defines a simulation and its initial state.
    inline void expect_same_simulation(const loaded_simulation& expected, const loaded_simulation& actual)
    {
        Assert::IsNotNull(expected.sim.get());
        Assert::IsNotNull(actual.sim.get());

        Assert::AreEqual(expected.sim->xmin(), actual.sim->xmin(), L"xmin");
        Assert::AreEqual(expected.sim->xmax(), actual.sim->xmax(), L"xmax");
        Assert::AreEqual(expected.sim->ymin(), actual.sim->ymin(), L"ymin");
        Assert::AreEqual(expected.sim->ymax(), actual.sim->ymax(), L"ymax");
        Assert::AreEqual(expected.sim->dt(), actual.sim->dt(), L"dt");
        Assert::AreEqual(expected.sim->iterations_per_update(), actual.sim->iterations_per_update(), L"iterations");
        Assert::AreEqual(expected.sim->speed(), actual.sim->speed(), L"speed");
        Assert::AreEqual(expected.sim->softening(), actual.sim->softening(), L"softening");
        Assert::AreEqual(expected.sim->min_gravitational_mass(), actual.sim->min_gravitational_mass(), L"min gravitational mass");
        Assert::AreEqual(expected.sim->walls().left, actual.sim->walls().left, L"left wall");
        Assert::AreEqual(expected.sim->walls().right, actual.sim->walls().right, L"right wall");
        Assert::AreEqual(expected.sim->walls().top, actual.sim->walls().top, L"top wall");
        Assert::AreEqual(expected.sim->walls().bottom, actual.sim->walls().bottom, L"bottom wall");

        const auto& em = expected.sim->model().masses();
        const auto& am = actual.sim->model().masses();
        Assert::AreEqual(em.size(), am.size(), L"mass count");
        Assert::AreEqual(expected.masses.size(), actual.masses.size(), L"mass state count");
        for (size_t i = 0; i < em.size(); ++i)
        {
            const std::wstring at = L" at mass " + std::to_wstring(i);
            Assert::AreEqual(em[i].m(), am[i].m(), (L"m" + at).c_str());
            Assert::AreEqual(em[i].r(), am[i].r(), (L"r" + at).c_str());
            Assert::AreEqual(em[i].e(), am[i].e(), (L"e" + at).c_str());
            Assert::AreEqual(em[i].colour().has_value(), am[i].colour().has_value(), (L"has colour" + at).c_str());
        Assert::AreEqual(em[i].label().has_value(), am[i].label().has_value(), (L"has label" + at).c_str());
        if (em[i].label() && am[i].label())
            Assert::IsTrue(*em[i].label() == *am[i].label(), (L"label" + at).c_str());
            if (em[i].colour() && am[i].colour())
                Assert::AreEqual(*em[i].colour(), *am[i].colour(), (L"colour" + at).c_str());

            const auto& es = expected.masses[i];
            const auto& as = actual.masses[i];
            Assert::AreEqual(es.position_.x(), as.position_.x(), (L"x" + at).c_str());
            Assert::AreEqual(es.position_.y(), as.position_.y(), (L"y" + at).c_str());
            Assert::AreEqual(es.velocity_.x(), as.velocity_.x(), (L"vx" + at).c_str());
            Assert::AreEqual(es.velocity_.y(), as.velocity_.y(), (L"vy" + at).c_str());
            Assert::AreEqual(es.acceleration_.x(), as.acceleration_.x(), (L"ax" + at).c_str());
            Assert::AreEqual(es.acceleration_.y(), as.acceleration_.y(), (L"ay" + at).c_str());
            Assert::AreEqual(es.force_.x(), as.force_.x(), (L"fx" + at).c_str());
            Assert::AreEqual(es.force_.y(), as.force_.y(), (L"fy" + at).c_str());
            Assert::AreEqual(es.angular_velocity_, as.angular_velocity_, (L"angular velocity" + at).c_str());
            Assert::AreEqual(es.angular_position_, as.angular_position_, (L"angular position" + at).c_str());
            Assert::AreEqual(es.fixed_, as.fixed_, (L"fixed" + at).c_str());
        }

        const auto& es = expected.sim->model().springs();
        const auto& as = actual.sim->model().springs();
        Assert::AreEqual(es.size(), as.size(), L"spring count");
        Assert::AreEqual(expected.springs.size(), actual.springs.size(), L"spring state count");
        for (size_t i = 0; i < es.size(); ++i)
        {
            const std::wstring at = L" at spring " + std::to_wstring(i);
            Assert::AreEqual(es[i].id_mass1(), as[i].id_mass1(), (L"a" + at).c_str());
            Assert::AreEqual(es[i].id_mass2(), as[i].id_mass2(), (L"b" + at).c_str());
            Assert::AreEqual(es[i].k(), as[i].k(), (L"k" + at).c_str());
            Assert::AreEqual(es[i].l0(), as[i].l0(), (L"l0" + at).c_str());
            Assert::AreEqual(es[i].min_factor(), as[i].min_factor(), (L"min" + at).c_str());
            Assert::AreEqual(es[i].max_factor(), as[i].max_factor(), (L"max" + at).c_str());
            Assert::AreEqual(es[i].colour().has_value(), as[i].colour().has_value(), (L"has colour" + at).c_str());
            if (es[i].colour() && as[i].colour())
                Assert::AreEqual(*es[i].colour(), *as[i].colour(), (L"colour" + at).c_str());
            Assert::AreEqual(expected.springs[i].broken_, actual.springs[i].broken_, (L"broken" + at).c_str());
        }

        const auto& ed = expected.sim->model().dampers();
        const auto& ad = actual.sim->model().dampers();
        Assert::AreEqual(ed.size(), ad.size(), L"damper count");
        Assert::AreEqual(expected.dampers.size(), actual.dampers.size(), L"damper state count");
        for (size_t i = 0; i < ed.size(); ++i)
        {
            const std::wstring at = L" at damper " + std::to_wstring(i);
            Assert::AreEqual(ed[i].id_mass1(), ad[i].id_mass1(), (L"a" + at).c_str());
            Assert::AreEqual(ed[i].id_mass2(), ad[i].id_mass2(), (L"b" + at).c_str());
            Assert::AreEqual(ed[i].k(), ad[i].k(), (L"k" + at).c_str());
            Assert::AreEqual(ed[i].l0(), ad[i].l0(), (L"l0" + at).c_str());
            Assert::AreEqual(expected.dampers[i].broken_, actual.dampers[i].broken_, (L"broken" + at).c_str());
        }
    }
}
