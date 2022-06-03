#pragma once
#include <cmath>

class double2
{
public:
    double2() : x_(0.0), y_(0.0) {}
    double2(double x, double y) : x_(x), y_(y) {}
    double2& operator=(const double2& r)
    {
        if (this != &r)
        {
            x_ = r.x_;
            y_ = r.y_;
        }
        return *this;
    }
    double2& operator+=(const double2& r)
    {
        x_ += r.x_;
        y_ += r.y_;
        return *this;
    }
    double2& operator-=(const double2& r)
    {
        x_ -= r.x_;
        y_ -= r.y_;
        return *this;
    }
    double2 operator+(const double2& r) const
    {
        return double2(x_ + r.x_, y_ + r.y_);
    }
    double2 operator-(const double2& r) const
    {
        return double2(x_ - r.x_, y_ - r.y_);
    }
    double2 operator/(double d) const
    {
        return double2(x_ / d, y_ / d);
    }
    double2 operator-() const
    {
        return double2(-x_, -y_);
    }
    double2 operator*(double r) const
    {
        return double2(r * x_, r * y_);
    }
    double modulus() const
    {
        return sqrt(x_ * x_ + y_ * y_);
    }
    double2 unit_vector() const
    {
        const auto mod = modulus();
        if (abs(mod) < std::numeric_limits<double>::epsilon())
            return double2();
        return double2(x_ / mod, y_ / mod);
    }
    double distance(const double2& r) const
    {
        const auto d = *this - r;
        const auto x2 = d.x_ * d.x_;
        const auto y2 = d.y_ * d.y_;
        return sqrt(x2 + y2);
    }

    double x() const { return x_; }
    double y() const { return y_; }
private:
    friend double2 operator*(double f, const double2& r);
    friend double dot_product(const double2& a, const double2& b);

    double x_;
    double y_;
};

double2 operator*(double f, const double2& r)
{
    return double2(f * r.x_, f * r.y_);
}

double dot_product(const double2& a, const double2& b)
{
    return a.x_ * b.x_ + a.y_ * b.y_;
}