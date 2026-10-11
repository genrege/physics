#pragma once

#include "constants.h"
#include "vector2.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

class mass
{
public:
    // `colour` is 0xRRGGBB and `label` is UTF-8 text; both are for display only.
    // An unset colour is the default colour; an unset label shows the mass, an empty one shows nothing.
    mass(double m, double r, double e = 1.0f, std::optional<uint32_t> colour = std::nullopt, std::optional<std::string> label = std::nullopt)
        : m_(m), r_(r), e_(e), colour_(colour), label_(std::move(label))
    {
    }

    const std::optional<uint32_t>& colour() const
    {
        return colour_;
    }

    const std::optional<std::string>& label() const
    {
        return label_;
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
    std::optional<uint32_t> colour_;
    std::optional<std::string> label_;
};
