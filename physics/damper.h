#pragma once

#include "spring.h"
#include <cmath>

class damper
{
public:
    damper(size_t id_mass1, size_t id_mass2, float k, float l0) : id_mass1_(id_mass1), id_mass2_(id_mass2), k_(k), l0_(l0) {}

    damper(size_t id_mass1, size_t id_mass2, const spring& spring) : id_mass1_(id_mass1), id_mass2_(id_mass2)
    {
        l0_ = spring.l0();
        k_ = spring.k();
    }

    size_t id_mass1() const { return id_mass1_; }
    size_t id_mass2() const { return id_mass2_; }
    
    float force(float length, float speed) const
    {
        return k_* speed * speed;
    }

    float k() const
    {
        return k_;
    }
private:
    size_t id_mass1_;
    size_t id_mass2_;

    float k_;      //damping factor
    float l0_;     //rest length
};

