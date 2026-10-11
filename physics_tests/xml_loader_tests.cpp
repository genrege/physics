#include "model_files.h"

#include <fstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace physics_test;

namespace
{
    // Written from the original hardcoded C++ examples when they were migrated to XML.
    struct golden
    {
        size_t masses, springs, dampers;
        double dt;
        size_t iterations;
    };

    const golden goldens[] = {
        { 8, 0, 0, 1.0, 100 },
        { 6, 8, 8, 0.1, 10 },
        { 1265, 4826, 4826, 0.05, 10 },
        { 3511, 0, 0, 2500.0, 1 },
        { 7, 0, 0, 0.1, 10 },
        { 274, 967, 967, 0.1, 10 },
        { 485, 0, 0, 0.1, 10 },
        { 57, 35, 35, 2e-05, 100 },
        { 493, 0, 0, 0.1, 10 },
    };

    std::filesystem::path write_temp(const char* name, const std::string& text)
    {
        const auto path = std::filesystem::temp_directory_path() / name;
        std::ofstream(path, std::ios::binary) << text;
        return path;
    }

    // Loading must fail and the message must mention `expected`.
    void expect_load_error(const char* name, const std::string& text, const std::string& expected)
    {
        std::string error;
        const auto loaded = load_model_file(write_temp(name, text), &error);
        Assert::IsNull(loaded.sim.get(), (L"expected failure for " + std::wstring(expected.begin(), expected.end())).c_str());
        Assert::IsTrue(error.find(expected) != std::string::npos, (L"error was: " + std::wstring(error.begin(), error.end())).c_str());
    }

    const std::string header = "<simulation xmin=\"-1\" xmax=\"1\" ymin=\"-1\" ymax=\"1\" dt=\"0.1\" iterations=\"2\">";
}

TEST_CLASS(xml_models)
{
public:
    TEST_METHOD(all_shipped_models_load_with_expected_contents)
    {
        for (int n = 1; n <= 9; ++n)
        {
            const auto loaded = load_model(n);
            const auto& g = goldens[n - 1];
            const auto context = L"simulation " + std::to_wstring(n);
            Assert::AreEqual(g.masses, loaded.sim->model().masses().size(), context.c_str());
            Assert::AreEqual(g.springs, loaded.sim->model().springs().size(), context.c_str());
            Assert::AreEqual(g.dampers, loaded.sim->model().dampers().size(), context.c_str());
            Assert::AreEqual(g.dt, loaded.sim->dt(), context.c_str());
            Assert::AreEqual(g.iterations, loaded.sim->iterations_per_update(), context.c_str());
            Assert::AreEqual(g.masses, loaded.masses.size(), context.c_str());
        }
    }

    TEST_METHOD(written_models_round_trip_exactly)
    {
        for (const size_t n : { size_t(2), size_t(50), size_t(300) })
        {
            const auto s = make_scenario(n, 21u + unsigned(n));
            const auto path = std::filesystem::temp_directory_path() / "physics_round_trip.xml";
            std::string save_error;
            Assert::IsTrue(save_simulation_xml(path.wstring(), *s.sim, s.masses, save_error));

            loaded_simulation original;
            original.sim = s.sim;
            original.masses = s.masses;
            original.springs = s.springs;
            original.dampers = s.dampers;
            expect_same_simulation(original, load_model_file(path));
        }
    }

    TEST_METHOD(optional_attributes_take_their_defaults)
    {
        const auto path = write_temp("physics_defaults.xml",
            header + "<masses><mass m=\"2\" r=\"1\" x=\"0\" y=\"0\"/><mass m=\"3\" r=\"1\" x=\"5\" y=\"0\" fixed=\"true\" e=\"0.5\" vx=\"1\" vy=\"-2\"/></masses>"
            "<springs><spring a=\"0\" b=\"1\" k=\"4\" l0=\"5\"/></springs><dampers><damper a=\"1\" b=\"0\" k=\"6\"/></dampers></simulation>");
        const auto loaded = load_model_file(path);

        Assert::AreEqual(10000.0, loaded.sim->min_gravitational_mass());
        const auto& masses = loaded.sim->model().masses();
        Assert::AreEqual(1.0, masses[0].e());
        Assert::AreEqual(0.5, masses[1].e());
        Assert::IsFalse(loaded.masses[0].fixed_);
        Assert::IsTrue(loaded.masses[1].fixed_);
        Assert::AreEqual(0.0, loaded.masses[0].velocity_.x());
        Assert::AreEqual(-2.0, loaded.masses[1].velocity_.y());
        Assert::AreEqual(0.5, loaded.sim->model().springs()[0].min_factor());
        Assert::AreEqual(1.5, loaded.sim->model().springs()[0].max_factor());
        Assert::AreEqual(size_t(1), loaded.sim->model().dampers()[0].id_mass1());
        Assert::AreEqual(0.0, loaded.sim->model().dampers()[0].l0());
        Assert::IsFalse(loaded.sim->walls().left);
        Assert::IsFalse(loaded.sim->walls().right);
        Assert::IsFalse(loaded.sim->walls().top);
        Assert::IsTrue(loaded.sim->walls().bottom);
    }

    TEST_METHOD(speed_defaults_to_the_old_pace_and_can_be_set_explicitly)
    {
        // dt 0.1 x 2 steps per frame x 60 frames per second = 12x real time.
        const auto implicit = load_model_file(write_temp("physics_speed_default.xml",
            header + "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\"/></masses></simulation>"));
        Assert::AreEqual(0.1 * 2.0 * 60.0, implicit.sim->speed());

        const auto explicit_speed = load_model_file(write_temp("physics_speed_explicit.xml",
            "<simulation xmin=\"-1\" xmax=\"1\" ymin=\"-1\" ymax=\"1\" dt=\"0.1\" iterations=\"2\" speed=\"86400\">"
            "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\"/></masses></simulation>"));
        Assert::AreEqual(86400.0, explicit_speed.sim->speed());

        const auto rewritten = std::filesystem::temp_directory_path() / "physics_speed_again.xml";
        std::string error;
        Assert::IsTrue(save_simulation_xml(rewritten.wstring(), *explicit_speed.sim, explicit_speed.masses, error));
        expect_same_simulation(explicit_speed, load_model_file(rewritten));

        const auto rewritten_default = std::filesystem::temp_directory_path() / "physics_speed_default_again.xml";
        Assert::IsTrue(save_simulation_xml(rewritten_default.wstring(), *implicit.sim, implicit.masses, error));
        expect_same_simulation(implicit, load_model_file(rewritten_default));
    }

    TEST_METHOD(softening_defaults_to_none_round_trips_and_rejects_negatives)
    {
        const auto plain = load_model_file(write_temp("physics_soft_default.xml", header + "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\"/></masses></simulation>"));
        Assert::AreEqual(0.0, plain.sim->softening());

        const auto soft = load_model_file(write_temp("physics_soft.xml",
            "<simulation xmin=\"-1\" xmax=\"1\" ymin=\"-1\" ymax=\"1\" dt=\"0.1\" iterations=\"2\" softening=\"0.25\"><masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\"/></masses></simulation>"));
        Assert::AreEqual(0.25, soft.sim->softening());

        const auto rewritten = std::filesystem::temp_directory_path() / "physics_soft_again.xml";
        std::string error;
        Assert::IsTrue(save_simulation_xml(rewritten.wstring(), *soft.sim, soft.masses, error));
        expect_same_simulation(soft, load_model_file(rewritten));

        expect_load_error("physics_bad_soft.xml", "<simulation xmin=\"-1\" xmax=\"1\" ymin=\"-1\" ymax=\"1\" dt=\"0.1\" iterations=\"2\" softening=\"-1\"/>", "softening");
    }

    TEST_METHOD(wall_attributes_choose_which_edges_bounce)
    {
        const auto path = write_temp("physics_walls.xml",
            "<simulation xmin=\"-1\" xmax=\"1\" ymin=\"-1\" ymax=\"1\" dt=\"0.1\" iterations=\"2\" bounce_left=\"true\" bounce_right=\"1\" bounce_top=\"true\" bounce_bottom=\"false\">"
            "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\"/></masses></simulation>");
        const auto loaded = load_model_file(path);
        Assert::IsTrue(loaded.sim->walls().left);
        Assert::IsTrue(loaded.sim->walls().right);
        Assert::IsTrue(loaded.sim->walls().top);
        Assert::IsFalse(loaded.sim->walls().bottom);

        // Written back and read again the same.
        const auto rewritten = std::filesystem::temp_directory_path() / "physics_walls_again.xml";
        std::string error;
        Assert::IsTrue(save_simulation_xml(rewritten.wstring(), *loaded.sim, loaded.masses, error));
        expect_same_simulation(loaded, load_model_file(rewritten));
    }

    TEST_METHOD(colours_load_round_trip_and_default_to_unset)
    {
        const auto path = write_temp("physics_colours.xml",
            header + "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\" colour=\"#FF8000\"/><mass m=\"1\" r=\"1\" x=\"5\" y=\"0\" colour=\"#00ff7f\"/><mass m=\"1\" r=\"1\" x=\"9\" y=\"0\"/></masses>"
            "<springs><spring a=\"0\" b=\"1\" k=\"1\" l0=\"5\" colour=\"#0000FF\"/><spring a=\"1\" b=\"2\" k=\"1\" l0=\"4\"/></springs></simulation>");
        const auto loaded = load_model_file(path);
        const auto& masses = loaded.sim->model().masses();
        Assert::AreEqual(uint32_t(0xFF8000), *masses[0].colour());
        Assert::AreEqual(uint32_t(0x00FF7F), *masses[1].colour());
        Assert::IsFalse(masses[2].colour().has_value());
        Assert::AreEqual(uint32_t(0x0000FF), *loaded.sim->model().springs()[0].colour());
        Assert::IsFalse(loaded.sim->model().springs()[1].colour().has_value());

        const auto rewritten = std::filesystem::temp_directory_path() / "physics_colours_again.xml";
        std::string error;
        Assert::IsTrue(save_simulation_xml(rewritten.wstring(), *loaded.sim, loaded.masses, error));
        expect_same_simulation(loaded, load_model_file(rewritten));
    }

    TEST_METHOD(malformed_colours_are_rejected)
    {
        for (const char* bad : { "red", "#12345", "#GGGGGG", "FF0000", "#FF00000" })
        {
            expect_load_error("physics_bad_colour.xml", header + "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\" colour=\"" + bad + "\"/></masses></simulation>", "#RRGGBB");
        }
    }

    TEST_METHOD(labels_load_round_trip_and_default_to_unset)
    {
        const auto path = write_temp("physics_labels.xml",
            header + "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\" label=\"Earth &amp; &quot;Moon&quot; &lt;1&gt; Caf\xC3\xA9\"/>"
            "<mass m=\"1\" r=\"1\" x=\"5\" y=\"0\" label=\"\"/><mass m=\"1\" r=\"1\" x=\"9\" y=\"0\"/></masses></simulation>");
        const auto loaded = load_model_file(path);
        const auto& masses = loaded.sim->model().masses();
        Assert::IsTrue(*masses[0].label() == std::string("Earth & \"Moon\" <1> Caf\xC3\xA9"));
        Assert::IsTrue(masses[1].label().has_value());
        Assert::IsTrue(masses[1].label()->empty());
        Assert::IsFalse(masses[2].label().has_value());

        const auto rewritten = std::filesystem::temp_directory_path() / "physics_labels_again.xml";
        std::string error;
        Assert::IsTrue(save_simulation_xml(rewritten.wstring(), *loaded.sim, loaded.masses, error));
        expect_same_simulation(loaded, load_model_file(rewritten));
    }

    TEST_METHOD(solar_system_bodies_are_all_named)
    {
        const auto loaded = load_model_file(models_dir() / "solar_system.xml");
        const auto& masses = loaded.sim->model().masses();
        for (const auto& m : masses)
        {
            Assert::IsTrue(m.label().has_value());
            Assert::IsFalse(m.label()->empty());
        }
        Assert::IsTrue(*masses[0].label() == "Sun");
        Assert::IsTrue(*masses[3].label() == "Earth");
        Assert::IsTrue(*masses[4].label() == "Moon");
        Assert::IsTrue(*masses[27].label() == "Neptune");
    }

    TEST_METHOD(milky_way_is_a_rotating_galaxy_with_a_flat_rotation_curve)
    {
        const auto loaded = load_model_file(models_dir() / "milky_way.xml");
        const auto& masses = loaded.sim->model().masses();
        Assert::AreEqual(size_t(50000), masses.size());
        Assert::IsTrue(*masses[0].label() == "Sun");
        Assert::IsTrue(*masses[1].label() == "Galactic centre");
        for (size_t i = 2; i < masses.size(); ++i)
            Assert::IsTrue(masses[i].label().has_value() && masses[i].label()->empty());

        const double kpc = 3.0856775814913673e19;
        const double sun_radius = loaded.masses[0].position_.modulus() / kpc;
        Assert::IsTrue(sun_radius > 8.0 && sun_radius < 8.8, (L"sun radius " + std::to_wstring(sun_radius)).c_str());
        const double sun_speed = loaded.masses[0].velocity_.modulus() / 1e3;
        Assert::IsTrue(sun_speed > 190.0 && sun_speed < 260.0, (L"sun speed " + std::to_wstring(sun_speed)).c_str());

        // The stars turn one way; the dark halo has no net rotation and is left out.
        double angular_momentum = 0.0, magnitude = 0.0;
        vector2 momentum;
        for (size_t i = 0; i < masses.size(); ++i)
        {
            if (*masses[i].colour() == 0x3C3658)
                continue;
            const auto& p = loaded.masses[i].position_;
            const auto& v = loaded.masses[i].velocity_;
            const double l = masses[i].m() * (p.x() * v.y() - p.y() * v.x());
            angular_momentum += l;
            magnitude += std::abs(l);
            momentum += v * masses[i].m();
        }
        Assert::IsTrue(angular_momentum > 0.3 * magnitude, (L"angular momentum " + std::to_wstring(angular_momentum / magnitude)).c_str());

        // Roughly flat: the typical orbital speed of the disc stars at 4, 8 and 12 kpc stays within a band.
        for (const double r_kpc : { 4.0, 8.0, 12.0 })
        {
            double sum = 0.0;
            size_t n = 0;
            for (size_t i = 0; i < masses.size(); ++i)
            {
                const auto& p = loaded.masses[i].position_;
                const auto& v = loaded.masses[i].velocity_;
                const double r = p.modulus() / kpc;
                if (masses[i].colour() && *masses[i].colour() != 0x3C3658 && r > r_kpc * 0.9 && r < r_kpc * 1.1)
                {
                    sum += (p.x() * v.y() - p.y() * v.x()) / p.modulus() / 1e3;
                    ++n;
                }
            }
            Assert::IsTrue(n > 100, (L"stars at " + std::to_wstring(r_kpc) + L": " + std::to_wstring(n)).c_str());
            const double mean = sum / double(n);
            Assert::IsTrue(mean > 150.0 && mean < 260.0, (L"mean speed at " + std::to_wstring(r_kpc) + L": " + std::to_wstring(mean)).c_str());
        }

        Assert::IsFalse(loaded.sim->walls().bottom);
        Assert::IsTrue(std::abs(loaded.sim->speed() / loaded.sim->dt() - 10.0) < 1e-9);
    }

    TEST_METHOD(solar_system_model_matches_real_orbits)
    {
        const auto loaded = load_model_file(models_dir() / "solar_system.xml");
        const auto& masses = loaded.sim->model().masses();
        Assert::AreEqual(size_t(30), masses.size());
        Assert::IsFalse(loaded.sim->walls().bottom);

        // Index 0 is the Sun, 3 the Earth and 4 the Moon.
        const double au = 1.495978707e11;
        Assert::IsTrue(masses[0].m() > 1.98e30 && masses[0].m() < 1.99e30);

        const auto earth_from_sun = loaded.masses[3].position_ - loaded.masses[0].position_;
        Assert::IsTrue(earth_from_sun.modulus() > 0.97 * au && earth_from_sun.modulus() < 1.03 * au);
        const auto earth_speed = (loaded.masses[3].velocity_ - loaded.masses[0].velocity_).modulus();
        Assert::IsTrue(earth_speed > 29.0e3 && earth_speed < 31.0e3);

        const auto moon_from_earth = loaded.masses[4].position_ - loaded.masses[3].position_;
        Assert::IsTrue(moon_from_earth.modulus() > 3.5e8 && moon_from_earth.modulus() < 4.1e8);
        const auto moon_speed = (loaded.masses[4].velocity_ - loaded.masses[3].velocity_).modulus();
        Assert::IsTrue(moon_speed > 0.9e3 && moon_speed < 1.15e3);

        // The system sits in its centre-of-mass frame.
        vector2 momentum;
        double scale = 0.0;
        for (size_t i = 0; i < masses.size(); ++i)
        {
            momentum += loaded.masses[i].velocity_ * masses[i].m();
            scale += masses[i].m() * loaded.masses[i].velocity_.modulus();
        }
        Assert::IsTrue(momentum.modulus() < 1e-9 * scale);
    }

    TEST_METHOD(solar_system_planets_and_moons_stay_in_orbit)
    {
        const auto loaded = load_model_file(models_dir() / "solar_system.xml");

        // parent index of every body after the Sun (see the order of bodies in the file)
        const std::vector<size_t> parent = { 0, 0, 0, 0, 3, 0, 5, 5, 0, 8, 8, 8, 8, 0, 13, 13, 13, 13, 13, 13, 13, 0, 21, 21, 21, 21, 21, 0, 27, 27 };
        Assert::AreEqual(parent.size(), loaded.masses.size());

        auto engine = make_cpu_engine();
        engine->upload(*loaded.sim, loaded.masses, loaded.springs, loaded.dampers);
        engine->step(8640, loaded.sim->dt());   // ten days

        std::vector<mass_state> after;
        auto springs = loaded.springs;
        auto dampers = loaded.dampers;
        engine->download(after, springs, dampers);

        for (size_t i = 1; i < after.size(); ++i)
        {
            const double before = loaded.masses[i].position_.distance(loaded.masses[parent[i]].position_);
            const double now = after[i].position_.distance(after[parent[i]].position_);
            // Planets move noticeably along eccentric orbits in ten days; moons must stay close to their circle.
            const double allowed = parent[i] == 0 ? 0.15 : 0.3;
            Assert::IsTrue(now > before * (1.0 - allowed) && now < before * (1.0 + allowed), (L"body " + std::to_wstring(i) + L" left its orbit").c_str());
        }
    }

    TEST_METHOD(a_bad_wall_value_is_rejected)
    {
        expect_load_error("physics_bad_wall.xml", "<simulation xmin=\"-1\" xmax=\"1\" ymin=\"-1\" ymax=\"1\" dt=\"0.1\" iterations=\"2\" bounce_top=\"sometimes\"/>", "true or false");
    }

    TEST_METHOD(invalid_files_are_rejected_with_a_useful_message)
    {
        std::string error;
        load_model_file(std::filesystem::temp_directory_path() / "physics_does_not_exist.xml", &error);
        Assert::IsTrue(error.find("cannot open file") != std::string::npos);

        expect_load_error("physics_bad1.xml", "<model/>", "<simulation>");
        expect_load_error("physics_bad2.xml", "<simulation xmin=\"-1\"/>", "missing attribute");
        expect_load_error("physics_bad3.xml", header + "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\" shine=\"high\"/></masses></simulation>", "unknown attribute 'shine'");
        expect_load_error("physics_bad4.xml", header + "<masses><mass m=\"abc\" r=\"1\" x=\"0\" y=\"0\"/></masses></simulation>", "finite number");
        expect_load_error("physics_bad5.xml", header + "<masses><mass m=\"0\" r=\"1\" x=\"0\" y=\"0\"/></masses></simulation>", "m > 0");
        expect_load_error("physics_bad6.xml", header + "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\"/></masses><springs><spring a=\"0\" b=\"3\" k=\"1\" l0=\"1\"/></springs></simulation>", "outside");
        expect_load_error("physics_bad7.xml", header + "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\"/></masses><springs><spring a=\"0\" b=\"0\" k=\"1\" l0=\"1\"/></springs></simulation>", "itself");
        expect_load_error("physics_bad8.xml", header + "<wheels/></simulation>", "not allowed");
        expect_load_error("physics_bad9.xml", header + "<masses><spring a=\"0\" b=\"1\" k=\"1\" l0=\"1\"/></masses></simulation>", "not allowed inside <masses>");
        expect_load_error("physics_bad10.xml", header + "<masses><mass m=\"1\" r=\"1\" x=\"0\" y=\"0\" fixed=\"maybe\"/></masses></simulation>", "true or false");
        expect_load_error("physics_bad11.xml", "<simulation xmin=\"1\" xmax=\"-1\" ymin=\"-1\" ymax=\"1\" dt=\"0.1\" iterations=\"2\"/>", "xmax > xmin");
        expect_load_error("physics_bad12.xml", header + "<masses><mass m=\"1\"", "malformed");
    }

    TEST_METHOD(failed_load_leaves_outputs_untouched)
    {
        std::vector<mass_state> masses(3);
        std::vector<spring_state> springs;
        std::vector<damper_state> dampers;
        std::string error;
        const auto sim = load_simulation_xml(write_temp("physics_bad_untouched.xml", "<model/>").wstring(), masses, springs, dampers, error);
        Assert::IsNull(sim.get());
        Assert::AreEqual(size_t(3), masses.size());
    }
};
