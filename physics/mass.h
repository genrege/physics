#pragma once

#include "constants.h"
#include "double2.h"

class mass
{
public:
    mass(float m, float r, float e = 1.0f) : m_(m), r_(r), e_(e)
    {
    }

    float m() const
    {
        return m_;
    }

    float r() const
    {
        return r_;
    }

    float e() const
    {
        return e_;
    }

private:
    float m_;                  //  kg
    float r_;                  //  m
    float e_;                  // elastic coefficient
};
