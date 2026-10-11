#pragma once

// Pair-wise physics shared by the CPU and CUDA engines so both evaluate identical formulas.
// The engines differ only in iteration/summation order, which is what the regression tests exercise.

#include "constants.h"
#include "vector2.h"

namespace physics_math
{
    // Force on the lower-indexed mass; the higher-indexed mass receives the negation.
    // Returns false when the pair is below the gravitational mass threshold. A softening length above zero
    // replaces the inverse-square law with a Plummer-softened one.
    PHYS_HD inline bool gravity_pair(double m_lo, double m_hi, const vector2& p_lo, const vector2& p_hi, double min_gravitational_mass, double softening, vector2& force_on_lo)
    {
        if (m_lo + m_hi < min_gravitational_mass)
            return false;

        if (softening > 0.0)
        {
            const auto d = p_hi - p_lo;
            const auto d2 = d.sum_squares() + softening * softening;
            const auto inverse_cube = 1.0 / (d2 * sqrt(d2));
            force_on_lo = (constants::G * m_lo * m_hi * inverse_cube) * d;
            return true;
        }

        const auto u = (p_hi - p_lo).unit_vector();
        const auto distance = p_lo.distance(p_hi);
        const auto G_mass_lo = constants::G * m_lo;
        const auto scalar_force = G_mass_lo * (m_hi / (distance * distance));
        force_on_lo = scalar_force * u;
        return true;
    }

    // Force on mass 1; mass 2 receives the negation.
    PHYS_HD inline vector2 spring_pair_force(const vector2& p1, const vector2& p2, double k, double l0)
    {
        const auto length = p1.distance(p2);
        const auto scalar_force = k * (length - l0);
        const auto u = (p2 - p1).unit_vector();
        return scalar_force * u;
    }

    // Force on mass 1; mass 2 receives the negation.
    PHYS_HD inline vector2 damper_pair_force(const vector2& A, const vector2& B, const vector2& vA, const vector2& vB, double k)
    {
        const auto damping_force = dot_product(k * (vA - vB), ((B - A) / (B - A).modulus()));
        const auto u = (B - A).unit_vector();
        return -(damping_force * u);
    }

    // Bounces off each enabled world edge: the velocity component into the wall is reversed with 8% loss and
    // the component along it loses 20%. The mass is not moved back, so a mass outside is pushed inward each step.
    PHYS_HD inline vector2 wall_bounce(const vector2& position, const vector2& velocity, double radius,
                                       double xmin, double xmax, double ymin, double ymax,
                                       bool left, bool right, bool top, bool bottom)
    {
        vector2 v = velocity;
        if (bottom && position.y() + radius > ymax)
            v = vector2(0.8 * v.x(), -0.92f * fabs(v.y()));
        if (top && position.y() - radius < ymin)
            v = vector2(0.8 * v.x(), 0.92f * fabs(v.y()));
        if (left && position.x() - radius < xmin)
            v = vector2(0.92f * fabs(v.x()), 0.8 * v.y());
        if (right && position.x() + radius > xmax)
            v = vector2(-0.92f * fabs(v.x()), 0.8 * v.y());
        return v;
    }

    PHYS_HD inline long long cell_coordinate(double v, double cell_size)
    {
        return static_cast<long long>(floor(v / cell_size));
    }

    PHYS_HD inline long long cell_key(long long x, long long y)
    {
        return static_cast<long long>((static_cast<unsigned long long>(x) << 32) ^ (static_cast<unsigned long long>(y) & 0xffffffffULL));
    }

    struct collision_delta
    {
        vector2 dpos_1;
        vector2 dpos_2;
        vector2 dvel_1;
        vector2 dvel_2;
    };

    // Evaluates a colliding pair from start-of-pass state; returns false if the pair does not collide.
    PHYS_HD inline bool collision_pair(const vector2& p1, const vector2& p2, const vector2& v1, const vector2& v2,
                                       double m1, double m2, double r1, double r2, double e1, double e2, collision_delta& out)
    {
        const double dx = p2.x() - p1.x();
        const double dy = p2.y() - p1.y();
        const double r2_dist = dx * dx + dy * dy;

        if (r2_dist <= 1.0e-12)
            return false;

        const double rsum = r1 + r2;
        if (r2_dist > rsum * rsum)
            return false;

        const double M = m1 + m2;
        const double distance = sqrt(r2_dist);
        const double overlap = (distance - r1 - r2);

        const double inv_r = 1.0 / distance;
        const double ux = dx * inv_r;
        const double uy = dy * inv_r;

        const double dux = overlap * ux;
        const double duy = overlap * uy;

        const double dr = m2 / M;
        out.dpos_1 = vector2(dr * dux, dr * duy);
        out.dpos_2 = -vector2((1 - dr) * dux, (1 - dr) * duy);

        const auto elastic_coeff = e1 * e2;
        const auto ideal_distance = r1 + r2;
        const auto ideal_distance_squared = ideal_distance * ideal_distance;
        const double recip_ideal_distance_squared = 1.0 / ideal_distance_squared;

        const vector2 dp = vector2(p1.x() - p2.x(), p1.y() - p2.y());

        const double dot1 = dot_product(v1 - v2, dp);
        const double dot2 = dot_product(v2 - v1, -dp);

        out.dvel_1 = -((2.0 * elastic_coeff * (m2 / M) * dot1 * recip_ideal_distance_squared) * dp);
        out.dvel_2 = -((2.0 * elastic_coeff * (m1 / M) * dot2 * recip_ideal_distance_squared) * (-dp));
        return true;
    }
}   // namespace physics_math
