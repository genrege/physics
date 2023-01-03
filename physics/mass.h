#pragma once

#include "constants.h"
#include "double2.h"

class mass
{
public:
    mass(double m, double r, double e = 1.0f) : m_(m), r_(r), e_(e)
    {
    }

    double m() const
    {
        return m_;
    }

    double r() const
    {
        return r_;
    }

    double e() const
    {
        return e_;
    }

private:
    double m_;                  //  kg
    double r_;                  //  m
    double e_;                  // elastic coefficient
};
