#pragma once

#include "simulation.h"

#include <memory>
#include <string>
#include <vector>

// Loads a simulation definition (world bounds, time step, masses, springs, dampers and the initial state)
// from XML. Returns nullptr and fills `error` on any problem; the output vectors are only modified on success.
//
// <simulation xmin ymin xmax ymax dt iterations [min_gravitational_mass] [softening]
//              [bounce_left] [bounce_right] [bounce_top] [bounce_bottom] [speed]>
//   bounce_* turn on bouncing off that edge of the world bounds; only the bottom edge (the floor) is on by default.
//   speed is the starting speed as a multiple of real time; if omitted it is dt * iterations * 60.
//   <masses>   <mass m r x y [e] [vx] [vy] [fixed] [colour] [label]/> ... </masses>
//   <springs>  <spring a b k l0 [min] [max] [colour]/> ...       </springs>
//   <dampers>  <damper a b k [l0]/> ...                 </dampers>
// </simulation>
// a and b are zero-based indices into <masses>; min/max are break limits as multiples of l0.
// colour is "#RRGGBB" and only affects drawing.
std::shared_ptr<simulation> load_simulation_xml(const std::wstring& path, std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers, std::string& error);

// Same format, from UTF-8 text (used for the clipboard).
std::shared_ptr<simulation> load_simulation_xml_text(const std::string& xml, std::vector<mass_state>& masses, std::vector<spring_state>& springs, std::vector<damper_state>& dampers, std::string& error);

// Writes the format above. Numbers use the shortest text that reads back exactly; default-valued optional attributes are omitted.
std::string simulation_to_xml(const simulation& sim, const std::vector<mass_state>& masses);
bool save_simulation_xml(const std::wstring& path, const simulation& sim, const std::vector<mass_state>& masses, std::string& error);
