#pragma once

#include <cmath>

namespace constants
{
    const double sqrt_2 = sqrtf(2);
    const double reciprocal_sqrt_2 = 1.0f / sqrt_2;
    constexpr double minimum_distance = 1.0E-15f;              //minimum distance in m between masses
    constexpr double plank_length = 1.0E-35f;              //m
    constexpr double G = 6.6743E-11f;           //gravitational constant m^3 kg^-1 s^-2
    constexpr double earth_mass = 5.972e24f;             //Mass of the Earth in kg
    constexpr double earth_radius = 6375000.0f;              //Radium of the Earth in m
    constexpr double lunar_mass = 7.3477e22f;             //Mass of the Moon in kg
    constexpr double lunar_radius = 1.737e6f;             //Radius of the Moon in ,
    constexpr double solar_mass = 1.989e30f;             //Solar mass in kg
    constexpr double solar_radius = 696340000.0f;            //Solar radius in m   
}   //namespace constants
