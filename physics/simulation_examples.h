#pragma once

#include "physics.h"
#include "simulation.h"
#include "coordinate_translation.h"

#include <memory>

std::shared_ptr<simulation> create_simulation_1(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states);
std::shared_ptr<simulation> create_simulation_2(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states);
std::shared_ptr<simulation> create_simulation_3(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states);
std::shared_ptr<simulation> create_simulation_4(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states);
std::shared_ptr<simulation> create_simulation_5(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states);
std::shared_ptr<simulation> create_simulation_6(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states);
std::shared_ptr<simulation> create_simulation_7(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states);
std::shared_ptr<simulation> create_simulation_8(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states);
std::shared_ptr<simulation> create_simulation_9(std::vector<mass_state>& mass_states, std::vector<spring_state>& spring_states, std::vector<damper_state>& damper_states);
