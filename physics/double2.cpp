#include "double2.h"
#include <cmath>


double2::double2() : x_(0.0), y_(0.0) {}
double2::double2(double x, double y) : x_(x), y_(y) {}
double2& double2::operator=(const double2& r)
{
    if (this != &r)
    {
        x_ = r.x_;
        y_ = r.y_;
    }
    return *this;
}
double2& double2::operator+=(const double2& r)
{
    x_ += r.x_;
    y_ += r.y_;
    return *this;
}
double2& double2::operator-=(const double2& r)
{
    x_ -= r.x_;
    y_ -= r.y_;
    return *this;
}
double2 double2::operator+(const double2& r) const
{
    return double2(x_ + r.x_, y_ + r.y_);
}
double2 double2::operator-(const double2& r) const
{
    return double2(x_ - r.x_, y_ - r.y_);
}
double2 double2::operator/(double d) const
{
    return double2(x_ / d, y_ / d);
}
double2 double2::operator-() const
{
    return double2(-x_, -y_);
}
double2 double2::operator*(double r) const
{
    return double2(r * x_, r * y_);
}
double double2::modulus() const
{
    return sqrt(x_ * x_ + y_ * y_);
}
double double2::sum_squares() const
{
    return x_ * x_ + y_ * y_;
}
double2 double2::unit_vector() const
{
    const auto mod = modulus();
    if (abs(mod) < std::numeric_limits<double>::epsilon())
        return double2();
    const auto rmod = 1 / mod;
    return double2(x_ * rmod, y_ * rmod);
}
double double2::distance(const double2& r) const
{
    const auto d = *this - r;
    const auto x2 = d.x_ * d.x_;
    const auto y2 = d.y_ * d.y_;
    return sqrt(x2 + y2);
}

bool double2::is_near_zero() const
{
    return abs(x_) < std::numeric_limits<double>::epsilon() && abs(y_) < std::numeric_limits<double>::epsilon();
}

double double2::x() const { return x_; }
double double2::y() const { return y_; }

double2 operator*(double f, const double2& r)
{
    return double2(f * r.x_, f * r.y_);
}

double dot_product(const double2& a, const double2& b)
{
    return a.x_ * b.x_ + a.y_ * b.y_;
}