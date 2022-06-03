#pragma once

#include "damper.h"
#include "mass.h"
#include "spring.h"

#include <vector>

//Represent the entities inside the system
class model_system
{
public:
    model_system() = default;

    model_system(const std::vector<mass>& masses, const std::vector<spring>& springs, const std::vector<damper>& dampers)
        : masses_(masses), springs_(springs), dampers_(dampers)
    {}

    const std::vector<mass>& masses() const { return masses_; }
    const std::vector<spring>& springs() const { return springs_; }
    const std::vector<damper>& dampers() const { return dampers_; }
private:
    std::vector<mass>       masses_;
    std::vector<spring>     springs_;
    std::vector<damper>     dampers_;
};
