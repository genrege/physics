#pragma once
#include <cmath>

class double2
{
public:
    double2() : x_(0.0), y_(0.0) {}
    double2(float x, float y) : x_(x), y_(y) {}
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
    double2 operator/(float d) const
    {
        return double2(x_ / d, y_ / d);
    }
    double2 operator-() const
    {
        return double2(-x_, -y_);
    }
    double2 operator*(float r) const
    {
        return double2(r * x_, r * y_);
    }
    float modulus() const
    {
        return sqrt(x_ * x_ + y_ * y_);
    }
    double2 unit_vector() const
    {
        const auto mod = modulus();
        if (abs(mod) < std::numeric_limits<float>::epsilon())
            return double2();
        const auto rmod = 1 / mod;
        return double2(x_ * rmod, y_ * rmod);
    }
    float distance(const double2& r) const
    {
        const auto d = *this - r;
        const auto x2 = d.x_ * d.x_;
        const auto y2 = d.y_ * d.y_;
        return sqrt(x2 + y2);
    }

    bool is_near_zero() const
    {
        return abs(x_) < std::numeric_limits<double>::epsilon() && abs(y_) < std::numeric_limits<double>::epsilon();
    }

    float x() const { return x_; }
    float y() const { return y_; }
private:
    friend double2 operator*(float f, const double2& r);
    friend float dot_product(const double2& a, const double2& b);

    float x_;
    float y_;
};

double2 operator*(float f, const double2& r)
{
    return double2(f * r.x_, f * r.y_);
}

float dot_product(const double2& a, const double2& b)
{
    return a.x_ * b.x_ + a.y_ * b.y_;
}