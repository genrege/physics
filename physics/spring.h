#pragma once

class spring
{
public:
    spring(size_t id_mass1, size_t id_mass2, double k, double l0, double min_length = 0.5, double max_length = 1.5)
        : id_mass1_(id_mass1), id_mass2_(id_mass2), k_(k), l0_(l0), min_length_(min_length), max_length_(max_length)
    {

    }

    size_t id_mass1() const { return id_mass1_; }
    size_t id_mass2() const { return id_mass2_; }

    double k() const { return k_; }
    double l0() const { return l0_; }

    double min_length() const { return l0_ * min_length_; }
    double max_length() const { return l0_ * max_length_; }

    double force(double length) const
    {
        return k_ * (length - l0_);
    }

private:
    size_t id_mass1_;
    size_t id_mass2_;

    double k_;      //young's modulus
    double l0_;     //rest length

    double min_length_;
    double max_length_;
};

