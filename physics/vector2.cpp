#include "vector2.h"
#include <cmath>
#include <limits>


vector2::vector2() : x_(0.0), y_(0.0) {}
vector2::vector2(double x, double y) : x_(x), y_(y) {}
vector2& vector2::operator=(const vector2& r)
{
    if (this != &r)
    {
        x_ = r.x_;
        y_ = r.y_;
    }
    return *this;
}
vector2& vector2::operator+=(const vector2& r)
{
    x_ += r.x_;
    y_ += r.y_;
    return *this;
}
vector2& vector2::operator-=(const vector2& r)
{
    x_ -= r.x_;
    y_ -= r.y_;
    return *this;
}
vector2 vector2::operator+(const vector2& r) const
{
    return vector2(x_ + r.x_, y_ + r.y_);
}
vector2 vector2::operator-(const vector2& r) const
{
    return vector2(x_ - r.x_, y_ - r.y_);
}
vector2 vector2::operator/(double d) const
{
    return vector2(x_ / d, y_ / d);
}
vector2 vector2::operator-() const
{
    return vector2(-x_, -y_);
}
vector2 vector2::operator*(double r) const
{
    return vector2(r * x_, r * y_);
}
double vector2::modulus() const
{
    return sqrt(x_ * x_ + y_ * y_);
}
double vector2::sum_squares() const
{
    return x_ * x_ + y_ * y_;
}
vector2 vector2::unit_vector() const
{
    const auto mod = modulus();
    if (abs(mod) < std::numeric_limits<double>::epsilon())
        return vector2();
    const auto rmod = 1.0 / mod;
    return vector2(x_ * rmod, y_ * rmod);
}
double vector2::distance(const vector2& r) const
{
    return sqrt(distance_squared(r));
}

double vector2::distance_squared(const vector2& r) const
{
    const auto d = *this - r;
    return d.x_ * d.x_ + d.y_ * d.y_;
}

double vector2::x() const { return x_; }
double vector2::y() const { return y_; }

vector2 operator*(double f, const vector2& r)
{
    return vector2(f * r.x_, f * r.y_);
}

double dot_product(const vector2& a, const vector2& b)
{
    return a.x_ * b.x_ + a.y_ * b.y_;
}