#include "cuda_runtime.h"
#include "mass.h"
#include "constants.h"
#include <vector>

#include "mass.h"
#include "simulation.h"


cudaError_t gravitational_force(std::vector<vector2>& f, const std::vector<mass>& masses, const std::vector<mass_state>& mass_states);



