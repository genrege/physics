#include "cuda_runtime.h"

#pragma once

class vector2
{
public:
    vector2();
    vector2(double x, double y);
    vector2& operator=(const vector2& r);
    vector2& operator+=(const vector2& r);
    vector2& operator-=(const vector2& r);
    vector2 operator+(const vector2& r) const;
    vector2 operator-(const vector2& r) const;
    vector2 operator/(double d) const;
    vector2 operator-() const;
    vector2 operator*(double r) const;
    double modulus() const;
    double sum_squares() const;
    vector2 unit_vector() const;
    double distance(const vector2& r) const;
    double distance_squared(const vector2& r) const;
    double x() const;
    double y() const;
private:
    friend vector2 operator*(double f, const vector2& r);
    friend double dot_product(const vector2& a, const vector2& b);

    double x_;
    double y_;
};

vector2 operator*(double f, const vector2& r);
double dot_product(const vector2& a, const vector2& b);
