#pragma once

#include "constants.h"
#include "double2.h"

class mass
{
public:
    mass(float m, float r) : m_(m), r_(r)
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

private:
    float m_;                  //  kg
    float r_;                  //  m
};
