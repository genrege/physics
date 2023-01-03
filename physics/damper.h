#pragma once

#include "spring.h"
#include <cmath>

class damper
{
public:
    damper(size_t id_mass1, size_t id_mass2, double k, double l0) : id_mass1_(id_mass1), id_mass2_(id_mass2), k_(k), l0_(l0) {}

    damper(size_t id_mass1, size_t id_mass2, const spring& spring) : id_mass1_(id_mass1), id_mass2_(id_mass2)
    {
        l0_ = spring.l0();
        k_ = spring.k();
    }

    size_t id_mass1() const { return id_mass1_; }
    size_t id_mass2() const { return id_mass2_; }
    
    double force(double length, double speed) const
    {
        return k_* speed * speed;
    }

    double k() const
    {
        return k_;
    }
private:
    size_t id_mass1_;
    size_t id_mass2_;

    double k_;      //damping factor
    double l0_;     //rest length
};

