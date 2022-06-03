#pragma once

class spring
{
public:
    spring(size_t id_mass1, size_t id_mass2, float k, float l0, float min_length = 0.5, float max_length = 1.5)
        : id_mass1_(id_mass1), id_mass2_(id_mass2), k_(k), l0_(l0), min_length_(min_length), max_length_(max_length)
    {

    }

    size_t id_mass1() const { return id_mass1_; }
    size_t id_mass2() const { return id_mass2_; }

    float k() const { return k_; }
    float l0() const { return l0_; }

    float min_length() const { return l0_ * min_length_; }
    float max_length() const { return l0_ * max_length_; }

    float force(float length, float speed) const
    {
        return k_ * (length - l0_);
    }

private:
    size_t id_mass1_;
    size_t id_mass2_;

    float k_;      //young's modulus
    float l0_;     //rest length

    float min_length_;
    float max_length_;
};

