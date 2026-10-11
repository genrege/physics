// Generates physics/models/milky_way.xml: a face-on, two-dimensional model of the Milky Way.
//
//   cl /O2 /openmp /EHsc /std:c++17 make_milky_way.cpp
//   make_milky_way.exe ..\physics\models\milky_way.xml [star count, default 100000]
//
// Components (fractions of the star count): central bulge and bar, thin disc with four trailing spiral arms,
// thick disc, stellar halo and a dark-matter halo whose surface density falls as 1/R so the rotation curve is flat.
// Every circular speed is measured from the model's own softened gravity, so the starting state is close to equilibrium.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double G = 4.30091e-6;           // kpc (km/s)^2 / Msun
    constexpr double kpc_m = 3.0856775814913673e19;
    constexpr double msun_kg = 1.98847e30;
    constexpr double star_radius_m = 2.0e16;
    constexpr double softening = 0.1;          // kpc; also written to the file so the simulation uses the same law
    constexpr double half_width = 30.0;        // kpc, the visible world

    enum class kind { bulge, bar, thin, arm, thick, halo, dark, sun };

    struct body
    {
        double x = 0, y = 0;     // kpc
        double vx = 0, vy = 0;   // km/s
        double mass = 0;         // Msun
        kind what = kind::thin;
        uint32_t colour = 0;
        std::string label;       // "" = no label
    };

    std::mt19937_64 rng(20261010);
    double uniform() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }
    double normal() { return std::normal_distribution<double>(0.0, 1.0)(rng); }

    uint32_t mix(uint32_t a, uint32_t b, double t)
    {
        auto channel = [&](int shift) {
            const double c = ((a >> shift) & 255) * (1 - t) + ((b >> shift) & 255) * t;
            return uint32_t(std::lround(c)) & 255u;
        };
        return (channel(16) << 16) | (channel(8) << 8) | channel(0);
    }

    // Exponential disc: surface density ~ exp(-R/h), so R follows a gamma(2, h) distribution.
    double disc_radius(double h, double lowest, double highest)
    {
        for (;;)
        {
            const double r = -h * (std::log(1.0 - uniform()) + std::log(1.0 - uniform()));
            if (r >= lowest && r <= highest)
                return r;
        }
    }

    // Two-dimensional Plummer sphere: M(<R) = R^2 / (R^2 + a^2).
    double plummer_radius(double a, double highest)
    {
        for (;;)
        {
            const double u = uniform();
            const double r = a * std::sqrt(u / (1.0 - u));
            if (r <= highest)
                return r;
        }
    }

    void place(body& b, double radius, double theta)
    {
        b.x = radius * std::cos(theta);
        b.y = radius * std::sin(theta);
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: make_milky_way output.xml [star count]\n");
        return 1;
    }
    const size_t total = argc > 2 ? size_t(std::strtoull(argv[2], nullptr, 10)) : 100000;
    const size_t n_bulge = total * 15 / 100, n_thin = total * 45 / 100, n_thick = total * 8 / 100, n_halo = total * 2 / 100;
    const size_t n_dark = total - n_bulge - n_thin - n_thick - n_halo;

    // Masses in Msun, chosen so the baryons total about 5e10 and the dark halo about 2e11 within 40 kpc.
    const double m_bulge = 1.5e10 / n_bulge, m_thin = 3.0e10 / n_thin, m_thick = 6.0e9 / n_thick, m_halo = 1.0e9 / n_halo;
    const double m_dark = 5.2e9 * 40.0 / n_dark;

    std::vector<body> bodies;
    bodies.reserve(total);

    // Bulge and bar: the bar is elongated and tilted 27 degrees from the Sun-centre line.
    const double bar_angle = 27.0 * pi / 180.0;
    for (size_t i = 0; i < n_bulge; ++i)
    {
        body b;
        b.mass = m_bulge;
        if (uniform() < 0.4)
        {
            double u, v;
            do { u = 1.3 * normal(); v = 0.4 * normal(); } while (std::abs(u) > 3.5 || std::abs(v) > 1.2);
            b.x = u * std::cos(bar_angle) - v * std::sin(bar_angle);
            b.y = u * std::sin(bar_angle) + v * std::cos(bar_angle);
            b.what = kind::bar;
            b.colour = mix(0xFFC46B, 0xFFE0A8, uniform());
        }
        else
        {
            place(b, plummer_radius(0.7, 3.0), 2 * pi * uniform());
            b.what = kind::bulge;
            b.colour = mix(0xFFD08A, 0xFFE9C4, uniform());
        }
        bodies.push_back(b);
    }

    // Thin disc: 40% sit in four trailing logarithmic spiral arms (pitch 12 degrees), the rest are smooth.
    const double pitch = 12.0 * pi / 180.0;
    for (size_t i = 0; i < n_thin; ++i)
    {
        body b;
        b.mass = m_thin;
        if (uniform() < 0.4)
        {
            const double r = disc_radius(3.2, 3.0, 17.0);
            const int arm = int(uniform() * 4.0) % 4;
            const double theta = arm * pi / 2.0 + 0.6 - std::log(r / 3.0) / std::tan(pitch) + 0.45 / r * normal();
            place(b, r, theta);
            b.what = kind::arm;
            b.colour = mix(0x8FA9FF, 0xD6E0FF, uniform());
        }
        else
        {
            place(b, disc_radius(2.6, 0.3, 18.0), 2 * pi * uniform());
            b.what = kind::thin;
            b.colour = mix(0xFFEFD8, 0xFFF8EE, uniform());
        }
        bodies.push_back(b);
    }

    for (size_t i = 0; i < n_thick; ++i)
    {
        body b;
        b.mass = m_thick;
        place(b, disc_radius(2.0, 0.3, 18.0), 2 * pi * uniform());
        b.what = kind::thick;
        b.colour = mix(0xFFC47A, 0xFFDDA8, uniform());
        bodies.push_back(b);
    }

    for (size_t i = 0; i < n_halo; ++i)
    {
        body b;
        b.mass = m_halo;
        place(b, plummer_radius(6.0, 30.0), 2 * pi * uniform());
        b.what = kind::halo;
        b.colour = mix(0xC8765A, 0xE59A7A, uniform());
        bodies.push_back(b);
    }

    for (size_t i = 0; i < n_dark; ++i)
    {
        body b;
        b.mass = m_dark;
        place(b, 40.0 * uniform(), 2 * pi * uniform());   // surface density ~ 1/R: mass grows linearly with R
        b.what = kind::dark;
        b.colour = 0x3C3658;
        bodies.push_back(b);
    }

    // The Sun replaces one thin-disc star, 8.2 kpc out between two arms; the star nearest the middle is the centre.
    {
        size_t thin_index = 0;
        for (size_t i = 0; i < bodies.size(); ++i)
        {
            if (bodies[i].what == kind::thin)
                thin_index = i;
        }
        bodies[thin_index].what = kind::sun;
        bodies[thin_index].colour = 0xFFE24A;
        bodies[thin_index].label = "Sun";
        place(bodies[thin_index], 8.2, pi / 2 + 0.35);

        size_t centre = 0;
        double best = 1e30;
        for (size_t i = 0; i < bodies.size(); ++i)
        {
            if ((bodies[i].what == kind::bulge || bodies[i].what == kind::bar) && std::hypot(bodies[i].x, bodies[i].y) < best)
            {
                best = std::hypot(bodies[i].x, bodies[i].y);
                centre = i;
            }
        }
        bodies[centre].label = "Galactic centre";
        bodies[centre].colour = 0xFFFFFF;
    }

    const long count = long(bodies.size());

    // Circular speed from the radial pull of the whole model, averaged in logarithmic radius bins.
    constexpr int bins = 70;
    constexpr double r_low = 0.1, r_high = 60.0;
    auto bin_of = [&](double r) {
        const double t = std::log(std::max(r, r_low) / r_low) / std::log(r_high / r_low);
        return std::min(bins - 1, std::max(0, int(t * bins)));
    };
    std::vector<std::vector<size_t>> members(bins);
    for (size_t i = 0; i < bodies.size(); ++i)
        members[bin_of(std::hypot(bodies[i].x, bodies[i].y))].push_back(i);

    std::vector<double> bin_radius(bins, 0.0), bin_vc(bins, -1.0);
    for (int k = 0; k < bins; ++k)
    {
        std::vector<size_t> targets = members[k];
        std::shuffle(targets.begin(), targets.end(), rng);
        if (targets.size() > 150)
            targets.resize(150);
        if (targets.empty())
            continue;

        double inward = 0.0, radius = 0.0;
#pragma omp parallel for reduction(+:inward, radius)
        for (long t = 0; t < long(targets.size()); ++t)
        {
            const auto& me = bodies[targets[t]];
            const double r = std::hypot(me.x, me.y);
            double ax = 0, ay = 0;
            for (long j = 0; j < count; ++j)
            {
                if (size_t(j) == targets[t])
                    continue;
                const double dx = bodies[j].x - me.x, dy = bodies[j].y - me.y;
                const double d2 = dx * dx + dy * dy + softening * softening;
                const double f = G * bodies[j].mass / (d2 * std::sqrt(d2));
                ax += f * dx;
                ay += f * dy;
            }
            inward += -(ax * me.x + ay * me.y) / r;
            radius += r;
        }
        bin_radius[k] = radius / double(targets.size());
        const double a_in = inward / double(targets.size());
        bin_vc[k] = a_in > 0 ? std::sqrt(a_in * bin_radius[k]) : -1.0;
    }
    // Fill empty or outward-pulled bins from their neighbours.
    for (int k = 0; k < bins; ++k)
    {
        if (bin_vc[k] > 0)
            continue;
        int lo = k - 1, hi = k + 1;
        while (lo >= 0 && bin_vc[lo] <= 0) --lo;
        while (hi < bins && bin_vc[hi] <= 0) ++hi;
        bin_vc[k] = lo >= 0 ? bin_vc[lo] : (hi < bins ? bin_vc[hi] : 100.0);
        if (bin_radius[k] <= 0)
            bin_radius[k] = r_low * std::pow(r_high / r_low, (k + 0.5) / bins);
    }
    auto circular_speed = [&](double r) {
        int k = bin_of(r);
        // linear interpolation between neighbouring bin centres in log radius
        const double lr = std::log(std::max(r, r_low));
        const int other = (lr > std::log(bin_radius[k]) ? k + 1 : k - 1);
        if (other < 0 || other >= bins)
            return bin_vc[k];
        const double l0 = std::log(bin_radius[k]), l1 = std::log(bin_radius[other]);
        if (l1 == l0)
            return bin_vc[k];
        const double t = std::min(1.0, std::max(0.0, (lr - l0) / (l1 - l0)));
        return bin_vc[k] * (1 - t) + bin_vc[other] * t;
    };

    // Velocities: +theta is clockwise on screen (y points down), like the Galaxy seen from the north.
    for (auto& b : bodies)
    {
        const double r = std::max(std::hypot(b.x, b.y), 1e-3);
        const double theta = std::atan2(b.y, b.x);
        const double vc = circular_speed(r);
        double v_phi = 0, v_r = 0;
        switch (b.what)
        {
        case kind::sun:
            v_phi = vc;
            break;
        case kind::thin:
        case kind::arm:
        {
            const double sigma = std::max(0.14 * vc, 8.0);
            v_r = sigma * normal();
            v_phi = vc + 0.7 * sigma * normal();
            break;
        }
        case kind::thick:
        {
            const double sigma = 0.28 * vc;
            v_r = sigma * normal();
            v_phi = 0.9 * vc + 0.7 * sigma * normal();
            break;
        }
        case kind::bulge:
        case kind::bar:
            v_r = 0.45 * vc * normal();
            v_phi = 0.6 * vc + 0.45 * vc * normal();
            break;
        case kind::halo:
            v_r = 0.55 * vc * normal();
            v_phi = 0.55 * vc * normal();
            break;
        case kind::dark:
            v_r = 0.7 * vc * normal();
            v_phi = 0.7 * vc * normal();
            break;
        }
        b.vx = v_r * std::cos(theta) - v_phi * std::sin(theta);
        b.vy = v_r * std::sin(theta) + v_phi * std::cos(theta);
    }

    // Remove any drift of the centre of mass.
    {
        double m = 0, cx = 0, cy = 0, px = 0, py = 0;
        for (const auto& b : bodies) { m += b.mass; cx += b.mass * b.x; cy += b.mass * b.y; px += b.mass * b.vx; py += b.mass * b.vy; }
        for (auto& b : bodies) { b.x -= cx / m; b.y -= cy / m; b.vx -= px / m; b.vy -= py / m; }
    }

    // Diagnostics: the virial ratio 2T/|W| should be close to 1 for a system in equilibrium.
    {
        double kinetic = 0.0, potential = 0.0;
        for (const auto& b : bodies)
            kinetic += 0.5 * b.mass * (b.vx * b.vx + b.vy * b.vy);

        std::vector<size_t> sample(bodies.size());
        for (size_t i = 0; i < sample.size(); ++i) sample[i] = i;
        std::shuffle(sample.begin(), sample.end(), rng);
        sample.resize(std::min<size_t>(sample.size(), 6000));
#pragma omp parallel for reduction(+:potential)
        for (long s = 0; s < long(sample.size()); ++s)
        {
            const auto& me = bodies[sample[s]];
            double phi = 0.0;
            for (long j = 0; j < count; ++j)
            {
                if (size_t(j) == sample[s]) continue;
                const double dx = bodies[j].x - me.x, dy = bodies[j].y - me.y;
                phi += G * bodies[j].mass / std::sqrt(dx * dx + dy * dy + softening * softening);
            }
            potential += 0.5 * me.mass * phi;   // each pair counted once overall
        }
        potential *= double(bodies.size()) / double(sample.size());
        std::printf("bodies %zu, kinetic %.3e, potential %.3e (Msun km^2/s^2), virial ratio 2T/|W| = %.3f\n", bodies.size(), kinetic, potential, 2.0 * kinetic / potential);

        for (const double r : { 1.0, 3.0, 8.2, 15.0, 30.0 })
            std::printf("v_c(%.1f kpc) = %.0f km/s\n", r, circular_speed(r));
    }

    // The two named stars come first in the file.
    std::stable_partition(bodies.begin(), bodies.end(), [](const body& b) { return b.what == kind::sun; });
    {
        const auto it = std::find_if(bodies.begin() + 1, bodies.end(), [](const body& b) { return !b.label.empty(); });
        if (it != bodies.end())
            std::rotate(bodies.begin() + 1, it, it + 1);
    }

    std::FILE* out = std::fopen(argv[1], "wb");
    if (!out)
    {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    const double edge = half_width * kpc_m;
    const double dt = 7.9e12;   // 0.25 million years
    std::fprintf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<simulation xmin=\"%.7g\" xmax=\"%.7g\" ymin=\"%.7g\" ymax=\"%.7g\" dt=\"%.7g\" iterations=\"1\" bounce_bottom=\"false\" speed=\"%.7g\" softening=\"%.7g\">\n  <masses>\n",
        -edge, edge, -edge, edge, dt, dt * 10.0, softening * kpc_m);
    for (const auto& b : bodies)
    {
        std::fprintf(out, "    <mass m=\"%.7g\" r=\"%.7g\" x=\"%.7g\" y=\"%.7g\" vx=\"%.7g\" vy=\"%.7g\" colour=\"#%06X\" label=\"%s\"/>\n",
            b.mass * msun_kg, star_radius_m, b.x * kpc_m, b.y * kpc_m, b.vx * 1e3, b.vy * 1e3, b.colour, b.label.c_str());
    }
    std::fprintf(out, "  </masses>\n</simulation>\n");
    std::fclose(out);
    std::printf("wrote %s\n", argv[1]);
    return 0;
}
