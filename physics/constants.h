#pragma once

#include <cmath>

namespace constants
{
    const double sqrt_2 = sqrt(2);
    const double reciprocal_sqrt_2 = 1.0 / sqrt_2;
    constexpr double minimum_distance = 1.0E-15;              //minimum distance in m between masses
    constexpr double plank_length = 1.0E-35;              //m
    constexpr double G = 6.6743E-11;           //gravitational constant m^3 kg^-1 s^-2
    constexpr double earth_mass = 5.972e24;             //Mass of the Earth in kg
    constexpr double earth_radius = 6375000;              //Radium of the Earth in m
    constexpr double lunar_mass = 7.3477e22;             //Mass of the Moon in kg
    constexpr double lunar_radius = 1.737e6;             //Radius of the Moon in ,
    constexpr double solar_mass = 1.989e30;             //Solar mass in kg
    constexpr double solar_radius = 696340000;            //Solar radius in m   
}   //namespace constants
