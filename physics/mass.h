#pragma once

#include "constants.h"
#include "double2.h"

class mass
{
public:
    mass(double m, double r) : m_(m), r_(r)
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

private:
    double m_;                  //  kg
    double r_;                  //  m
};
