#pragma once

class double2
{
public:
    double2();
    double2(double x, double y);
    double2& operator=(const double2& r);
    double2& operator+=(const double2& r);
    double2& operator-=(const double2& r);
    double2 operator+(const double2& r) const;
    double2 operator-(const double2& r) const;
    double2 operator/(double d) const;
    double2 operator-() const;
    double2 operator*(double r) const;
    double modulus() const;
    double sum_squares() const;
    double2 unit_vector() const;
    double distance(const double2& r) const;
    bool is_near_zero() const;
    double x() const;
    double y() const;
private:
    friend double2 operator*(double f, const double2& r);
    friend double dot_product(const double2& a, const double2& b);

    double x_;
    double y_;
};

double2 operator*(double f, const double2& r);
double dot_product(const double2& a, const double2& b);
