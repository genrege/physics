#pragma once

#include <cmath>

#ifdef __CUDACC__
#define PHYS_HD __host__ __device__
#else
#define PHYS_HD
#endif

class vector2
{
public:
    PHYS_HD vector2() : x_(0.0), y_(0.0) {}
    PHYS_HD vector2(double x, double y) : x_(x), y_(y) {}

    PHYS_HD vector2& operator+=(const vector2& r) { x_ += r.x_; y_ += r.y_; return *this; }
    PHYS_HD vector2& operator-=(const vector2& r) { x_ -= r.x_; y_ -= r.y_; return *this; }
    PHYS_HD vector2 operator+(const vector2& r) const { return vector2(x_ + r.x_, y_ + r.y_); }
    PHYS_HD vector2 operator-(const vector2& r) const { return vector2(x_ - r.x_, y_ - r.y_); }
    PHYS_HD vector2 operator/(double d) const { return vector2(x_ / d, y_ / d); }
    PHYS_HD vector2 operator-() const { return vector2(-x_, -y_); }
    PHYS_HD vector2 operator*(double r) const { return vector2(r * x_, r * y_); }

    PHYS_HD double modulus() const { return sqrt(x_ * x_ + y_ * y_); }
    PHYS_HD double sum_squares() const { return x_ * x_ + y_ * y_; }

    PHYS_HD vector2 unit_vector() const
    {
        constexpr double epsilon = 2.220446049250313e-16;   // std::numeric_limits<double>::epsilon()
        const auto mod = modulus();
        if (fabs(mod) < epsilon)
            return vector2();
        const auto rmod = 1.0 / mod;
        return vector2(x_ * rmod, y_ * rmod);
    }

    PHYS_HD double distance(const vector2& r) const { return sqrt(distance_squared(r)); }

    PHYS_HD double distance_squared(const vector2& r) const
    {
        const auto d = *this - r;
        return d.x_ * d.x_ + d.y_ * d.y_;
    }

    PHYS_HD double x() const { return x_; }
    PHYS_HD double y() const { return y_; }

    PHYS_HD friend vector2 operator*(double f, const vector2& r) { return vector2(f * r.x_, f * r.y_); }
    PHYS_HD friend double dot_product(const vector2& a, const vector2& b) { return a.x_ * b.x_ + a.y_ * b.y_; }

private:
    double x_;
    double y_;
};
