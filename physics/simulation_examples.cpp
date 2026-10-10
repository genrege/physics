#include "simulation_examples.h"


std::shared_ptr<simulation> create_simulation_1(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states)
{
    std::vector<mass> masses;
    simulation::add_mass(
        constants::earth_mass,
        constants::earth_radius,
        { 0.0, 0.0 },
        {},
        {},
        false,
        masses,
        mass_states);

    simulation::add_mass(
        constants::earth_mass,
        constants::earth_radius,
        { -20.0 * constants::earth_radius, constants::earth_radius * 38.0 },
        { 500.0, 300.0 },
        {},
        false,
        masses,
        mass_states);

    simulation::add_mass(
        constants::earth_mass * 0.6f,
        constants::earth_radius * 0.2f,
        { 30.0 * constants::earth_radius, -constants::earth_radius * 48.0 },
        { -900.0, -600.0 },
        {},
        false,
        masses,
        mass_states);

    simulation::add_mass(
        constants::lunar_mass,
        constants::lunar_radius,
        { -constants::earth_radius * 5, constants::earth_radius * 5 },
        { -1700, -2700 },
        {},
        false,
        masses,
        mass_states);

    simulation::add_mass(
        constants::lunar_mass,
        constants::lunar_radius,
        { -constants::earth_radius * 6.5, constants::earth_radius * 3 },
        { -1900, -3200 },
        {},
        false,
        masses,
        mass_states);

    simulation::add_mass(
        constants::lunar_mass,
        constants::lunar_radius,
        { -constants::earth_radius * 7, constants::earth_radius * 5 },
        { -1900, -1200 },
        {},
        false,
        masses,
        mass_states);

    simulation::add_mass(
        75.0,
        10000,
        { constants::earth_radius + 200000.0, constants::earth_radius + 200000.0 },
        { 5500, -3700 },
        {},
        false,
        masses,
        mass_states);

    simulation::add_mass(
        9000,
        40000,
        { -4 * constants::earth_radius - 200000.0, -6 * constants::earth_radius },
        { 3200, 0 },
        {},
        false,
        masses,
        mass_states);

    std::vector<spring> springs;

    std::vector<damper> dampers;

    return std::make_shared<simulation>(model_system(masses, springs, dampers), -300000000, 700000000, -300000000, 700000000, 1, 100);
}

std::shared_ptr<simulation> create_simulation_2(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states)
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    simulation::add_mass(constants::earth_mass * 0.001f, 20.0f, { 0.0f, constants::earth_radius }, { 0.0f, 0.0f }, {}, false, masses, mass_states);

    const auto m0 = simulation::add_mass(100, 20, { 500, 100 }, { -3,0 }, {}, false, masses, mass_states);
    const auto m1 = simulation::add_mass(100, 20, { 600, 100 }, {}, {}, false, masses, mass_states);
    const auto m2 = simulation::add_mass(100, 20, { 500, 200 }, {}, {}, false, masses, mass_states);
    const auto m3 = simulation::add_mass(100, 20, { 600, 200 }, {}, {}, false, masses, mass_states);
    const auto m4 = simulation::add_mass(50, 20, { 550, 150 }, {}, {}, false, masses, mass_states);

    simulation::add_spring(m0, m1, true, 10, 100, springs, spring_states);
    simulation::add_spring(m0, m2, true, 10, 100, springs, spring_states);
    simulation::add_spring(m1, m3, true, 10, 100, springs, spring_states);
    simulation::add_spring(m2, m3, true, 10, 100, springs, spring_states);
    simulation::add_spring(m0, m4, true, 10, 75, springs, spring_states);
    simulation::add_spring(m1, m4, true, 10, 75, springs, spring_states);
    simulation::add_spring(m2, m4, true, 10, 75, springs, spring_states);
    simulation::add_spring(m3, m4, true, 10, 75, springs, spring_states);

    simulation::add_damper(m0, m1, 0.6, 700, dampers, damper_states);
    simulation::add_damper(m0, m2, 0.6, 700, dampers, damper_states);
    simulation::add_damper(m1, m3, 0.6, 700, dampers, damper_states);
    simulation::add_damper(m2, m3, 0.6, 700, dampers, damper_states);
    simulation::add_damper(m0, m4, 0.1, 700, dampers, damper_states);
    simulation::add_damper(m1, m4, 0.1, 700, dampers, damper_states);
    simulation::add_damper(m2, m4, 0.1, 700, dampers, damper_states);
    simulation::add_damper(m3, m4, 0.1, 700, dampers, damper_states);

    return std::make_shared<simulation>(model_system(masses, springs, dampers), -4000, 4000, -4000, 4000, 0.1, 10);
}

std::shared_ptr<simulation> create_simulation_3(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states)
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    size_t rows = 30;
    size_t cols = 42;

    double xdist = 100;
    double ydist = 100;
    double ddist = sqrt(xdist * xdist + ydist * ydist);
    double radius = 25;
    double ks = 3.1f;

    for (size_t r = 0; r < rows; ++r)
    {
        double y = -3500 + (double)r * xdist;
        for (size_t c = 0; c < cols; ++c)
        {
            double x = -5000 + (double)c * ydist;

            const auto fixed = (r < 5 && (c < cols / 4 || c > 3 * cols / 4));// || (r == 0 && c == cols / 2);

            const auto mass_id = r < rows / 2 ?
                simulation::add_mass(12., radius, vector2{ x, y }, vector2{ 0, 0 }, vector2{}, fixed, masses, mass_states)
                : simulation::add_mass(34., radius, vector2{ x, y }, vector2{ 0, 0 }, vector2{}, fixed, masses, mass_states);

            if (r < rows - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r + 1) * cols + c, true, (double)rows - r, ydist, springs, spring_states);
                simulation::add_damper(mass_id, (r + 1) * cols + c, springs[spring_id], dampers, damper_states);
            }
            if (c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r)*cols + c + 1, true, ks, xdist, springs, spring_states);
                simulation::add_damper(mass_id, (r)*cols + c + 1, springs[spring_id], dampers, damper_states);
            }
            if (r < rows - 1 && c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r + 1) * cols + c + 1, true, (double)rows - r, ddist, springs, spring_states);
                simulation::add_damper(mass_id, (r + 1) * cols + c + 1, springs[spring_id], dampers, damper_states);
            }
            if (r > 0 && c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r - 1) * cols + c + 1, true, ks, ddist, springs, spring_states);
                simulation::add_damper(mass_id, (r - 1) * cols + c + 1, springs[spring_id], dampers, damper_states);
            }
        }
    }
    simulation::add_mass(constants::earth_mass * 0.0525f, 20.0f, { 0.0f, constants::earth_radius }, { 0.0f, 0.0f }, {}, false, masses, mass_states);

    simulation::add_mass(20, 3 * radius, { 3800, 3800 }, { -42.0f, -68.0f }, {}, false, masses, mass_states);
    simulation::add_mass(580, 3 * radius, { 4500, -1800 }, { -80.0f, -12.0f }, {}, false, masses, mass_states);
    simulation::add_mass(250, 3 * radius, { 5100, 3800 }, { -90.0f, -100.0f }, {}, false, masses, mass_states);
    simulation::add_mass(650, 3 * radius, { -7200, 3800 }, { 32.0f, -72.0f }, {}, false, masses, mass_states);

    return std::make_shared<simulation>(model_system(masses, springs, dampers), -5000, 5000, -5000, 5000, 0.05, 10);
}

std::shared_ptr<simulation> create_simulation_4(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states)
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    const auto m = constants::lunar_mass * 2.45;
    const auto r = constants::lunar_radius * 3.0;

    for (double x = -25; x < 20; ++x)
    {
        for (double y = -20; y < 19; ++y)
        {
            simulation::add_mass(m, r * 4.0, 0.99, { (x) * -30 * constants::lunar_radius, (y - 20) * -30 * constants::lunar_radius }, { 600 - x * 10, -500 - y * 5 }, {}, false, masses, mass_states);

            simulation::add_mass(m, r * 4.0, 0.99, { (x) * 30 * constants::lunar_radius, (y - 20) * 30 * constants::lunar_radius }, { -500 + x * 5,  400 + y * 5 }, {}, false, masses, mass_states);
        }
    }

    simulation::add_mass(constants::earth_mass * 40.0f, r * 10, 0.99, {0, 0}, { 0, 0 }, {}, false, masses, mass_states);
    //simulation::add_mass(constants::earth_mass * 0.5f, r * 8, 0.97, { -60.0f * 30 * constants::lunar_radius, 0.0f }, { -500.0f, 0.0f }, {}, false, masses, mass_states);
    //simulation::add_mass(constants::earth_mass * 0.4f, r * 6, 0.96, { -constants::earth_radius * 40, constants::earth_radius * 90 }, { 1200.0f, 400.0f }, {}, false, masses, mass_states);

    return std::make_shared<simulation>(model_system(masses, springs, dampers), -500 * 30 * constants::lunar_radius, 500 * 30 * constants::lunar_radius, -500 * 30 * constants::lunar_radius, 500 * 30 * constants::lunar_radius, 2500, 1);
}

std::shared_ptr<simulation> create_simulation_5(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states)
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    simulation::add_mass(1, 40, 0.99, { -2000.0, 0.0 }, { 2.0, 0.0 }, { 0.0, 0.0 }, false, masses, mass_states);
    simulation::add_mass(1, 40, 0.99, { -1000.0, 0.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }, false, masses, mass_states);
    simulation::add_mass(1, 40, 0.99, { 800.0, 20.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }, false, masses, mass_states);
    simulation::add_mass(1, 40, 0.99, { 950.0, 20.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }, false, masses, mass_states);
    simulation::add_mass(1, 40, 0.99, { 1200.0, -10.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }, false, masses, mass_states);
    simulation::add_mass(1, 40, 0.99, { 1300.0, 15.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }, false, masses, mass_states);
    simulation::add_mass(1, 40, 0.99, { 1500.0, 30.0 }, { 0.0, 0.0 }, { 0.0, 0.0 }, false, masses, mass_states);

    return std::make_shared<simulation>(model_system(masses, springs, dampers), -1000, 1000, -1000, 1000, 0.1, 10);
}

std::shared_ptr<simulation> create_simulation_6(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states)
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    size_t rows = 9;
    size_t cols = 30;

    double xdist = 100;
    double ydist = 100;
    double ddist = sqrt(xdist * xdist + ydist * ydist);
    double radius = 30;

    double spring_k = 1.0;

    for (size_t r = 0; r < rows; ++r)
    {
        double y = -1400 + (double)r * xdist;
        for (size_t c = 0; c < cols; ++c)
        {
            double x = -3500 + (double)c * ydist;

            const auto fixed = false;

            const auto mass_id = r < rows / 2 ?
                simulation::add_mass(1., radius, vector2{ x, y }, vector2{ 7, 0 }, vector2{}, fixed, masses, mass_states)
                : simulation::add_mass(0.8, radius, vector2{ x, y }, vector2{ -5, 0 }, vector2{}, fixed, masses, mass_states);

            if (r < rows - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r + 1) * cols + c, true, spring_k, ydist, springs, spring_states);
                simulation::add_damper(mass_id, (r + 1) * cols + c, springs[spring_id], dampers, damper_states);
            }
            if (c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r)*cols + c + 1, true, spring_k, xdist, springs, spring_states);
                simulation::add_damper(mass_id, (r)*cols + c + 1, springs[spring_id], dampers, damper_states);
            }
            if (r < rows - 1 && c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r + 1) * cols + c + 1, true, spring_k, ddist, springs, spring_states);
                simulation::add_damper(mass_id, (r + 1) * cols + c + 1, springs[spring_id], dampers, damper_states);
            }
            if (r > 0 && c < cols - 1)
            {
                const auto spring_id = simulation::add_spring(mass_id, (r - 1) * cols + c + 1, true, spring_k, ddist, springs, spring_states);
                simulation::add_damper(mass_id, (r - 1) * cols + c + 1, springs[spring_id], dampers, damper_states);
            }
        }
    }

    simulation::add_mass(constants::earth_mass * 0.0015f, 20.0f, { 0.0f, constants::earth_radius }, { 0.0f, 0.0f }, {}, false, masses, mass_states);

    static int cb = 4;
    for (int i = 1; i < cb; ++i)
    {
        const auto id_mass = simulation::add_mass(1.0f + 6 * i, 20 + 3.0f * i, { -2500 - i * 100.0f, -2500 - i * 35.0f }, { 0, -5.0f }, {}, false, masses, mass_states);
        if (i > 1)
        {
            const auto id_spring = simulation::add_spring(id_mass - 1, id_mass, true, 1.0, 140.0f, springs, spring_states);
            simulation::add_damper(id_mass - 1, id_mass, springs[id_spring], dampers, damper_states);
        }
    }

    return std::make_shared<simulation>(model_system(masses, springs, dampers), -4000, 3000, -2000, 5000, 0.1, 10);
}

std::shared_ptr<simulation> create_simulation_7(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states)
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    size_t rows = 22;
    size_t cols = 22;

    double xdist = 100;
    double ydist = 100;
    double radius = 25;

    for (size_t r = 0; r < rows; ++r)
    {
        double y = -1600 + (double)r * xdist;
        for (size_t c = 0; c < cols; ++c)
        {
            double x = -3500 + (double)c * ydist;

            const auto fixed = false;

            simulation::add_mass(1., radius, 0.99, vector2{ x, y }, vector2{ 3.0f - r % 2, 0 }, vector2{}, fixed, masses, mass_states);
        }
    }
    simulation::add_mass(constants::earth_mass * 0.01f, 20.0f, { 0.0f, constants::earth_radius }, { 0.0f, 0.0f }, {}, false, masses, mass_states);

    return std::make_shared<simulation>(model_system(masses, springs, dampers), -4000, 3000, -2000, 5000, 0.1, 10);
}

std::shared_ptr<simulation> create_simulation_8(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states)
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    for (int i = 0; i < 25; ++i)
    {

        if (i <= 6)
        {

            const auto id_anchor = simulation::add_mass(0.5, 0.01, { i * 0.05f, 0.0f }, { 0, 0 }, {}, true, masses, mass_states);
            const auto id_ball = simulation::add_mass(0.5, 0.025, 0.995, { i * 0.05f, 0.4f }, { i == 0 ? -2.5f : 0, 0 }, {}, false, masses, mass_states);

            const auto id_spring = simulation::add_spring(id_ball, id_anchor, true, 1500.0, 0.4f, springs, spring_states);
            simulation::add_damper(id_ball, id_anchor, i < 7 ? 100.0 : 10.0, 0.4f, dampers, damper_states);
        }
        else if (i > 10)
        {
            const auto id_anchor = simulation::add_mass(0.5, 0.01, { i * 0.05f, 0.0f }, { 0, 0 }, {}, true, masses, mass_states);
            const auto id_ball2 = simulation::add_mass(0.3, 0.025, 0.995, { i * 0.05f, 0.4f }, { i == 24 ? 3.5f : 0, 0 }, {}, false, masses, mass_states);
            const auto id_spring = simulation::add_spring(id_anchor, id_ball2, true, 1500.0, 0.4f, springs, spring_states);
            simulation::add_damper(id_ball2, id_anchor, i < 7 ? 100.0 : 10.0, 0.2f, dampers, damper_states);

            const auto id_ball3 = simulation::add_mass(0.95, 0.01, 0.995, { i * 0.05f, 0.6f }, { i == 24 ? 3.0f : 0, 0 }, {}, false, masses, mass_states);
            const auto id_spring2 = simulation::add_spring(id_ball2, id_ball3, true, 800.0, 0.2f, springs, spring_states);
            simulation::add_damper(id_ball2, id_ball3, i < 7 ? 100.0 : 10.0, 0.2f, dampers, damper_states);
        }
    }

    simulation::add_mass(constants::earth_mass * 1.0f, 20.0f, { 0.0f, constants::earth_radius }, { 0.0f, 0.0f }, {}, false, masses, mass_states);

    return std::make_shared<simulation>(model_system(masses, springs, dampers), -0.2, 1, -0.2, 1, 0.00002, 100);
}

std::shared_ptr<simulation> create_simulation_9(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states)
{
    std::vector<mass> masses;
    std::vector<spring> springs;
    std::vector<damper> dampers;

    for (int r = 0; r < 20; ++r)
    {
        for (int c = 0; c < 8; ++c)
        {
            const auto idcL = simulation::add_mass(1000.0, 20.0, 0.9, { -c * 40.0 - 240.0, 1000.0 - r * 20.0 }, {}, {}, true, masses, mass_states);
            const auto idcR = simulation::add_mass(1000.0, 20.0, 0.9, { c * 40.0 + 240.0, 1000.0 - r * 20.0 }, {}, {}, true, masses, mass_states);
        }
    }

    for (int c = 0; c < 6; ++c)
    {
        const auto idcL = simulation::add_mass(1000.0, 20.0, 0.8, { -c * 40.0, 980.0 }, {}, {}, true, masses, mass_states);
        const auto idcR = simulation::add_mass(1000.0, 20.0, 0.7, { c * 40.0, 980.0 }, {}, {}, true, masses, mass_states);
    }

    for (int r = 0; r < 20; ++r)
    {
        for (int c = 0; c < 8; ++c)
        {
            const auto idM = simulation::add_mass(2.0, 20.0, 0.9, { -c * 15.0 - 20.0, -650.0 - r * 30.0 }, {}, {}, false, masses, mass_states);
        }
    }

    simulation::add_mass(constants::earth_mass * 0.01f, 20.0f, { 0.0f, constants::earth_radius }, { 0.0f, 0.0f }, {}, false, masses, mass_states);

    return std::make_shared<simulation>(model_system(masses, springs, dampers), -1000, 1000, -1000, 1000, 0.1, 10);
}

