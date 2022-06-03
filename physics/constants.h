#pragma once

#include <cmath>

namespace constants
{
    const float sqrt_2 = sqrtf(2);
    const float reciprocal_sqrt_2 = 1.0f / sqrt_2;
    constexpr float minimum_distance = 1.0E-15f;              //minimum distance in m between masses
    constexpr float plank_length = 1.0E-35f;              //m
    constexpr float G = 6.6743E-11f;           //gravitational constant m^3 kg^-1 s^-2
    constexpr float earth_mass = 5.972e24f;             //Mass of the Earth in kg
    constexpr float earth_radius = 6375000.0f;              //Radium of the Earth in m
    constexpr float lunar_mass = 7.3477e22f;             //Mass of the Moon in kg
    constexpr float lunar_radius = 1.737e6f;             //Radius of the Moon in ,
    constexpr float solar_mass = 1.989e30f;             //Solar mass in kg
    constexpr float solar_radius = 696340000.0f;            //Solar radius in m   
}   //namespace constants
