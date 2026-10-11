#pragma once

#include <cstdio>
#include <string>

// Text helpers for the info label; kept free of Windows code so they can be unit tested.

// Simulated time with a unit that suits its size: ms, s, min, h, d, yr (365.25 days), kyr, Myr or Gyr.
inline std::wstring format_duration(double seconds)
{
    if (!(seconds >= 0.0))
        seconds = 0.0;

    struct unit { double size; const wchar_t* name; };
    // Largest first; the first unit the value reaches wins.
    constexpr double year = 365.25 * 86400.0;
    static const unit units[] = {
        { 1e9 * year, L"Gyr" }, { 1e6 * year, L"Myr" }, { 1e3 * year, L"kyr" }, { year, L"yr" }, { 86400.0, L"d" }, { 3600.0, L"h" }, { 60.0, L"min" }, { 1.0, L"s" }
    };

    wchar_t text[64];
    for (const auto& u : units)
    {
        if (seconds >= u.size)
        {
            swprintf_s(text, L"%.2f %s", seconds / u.size, u.name);
            return text;
        }
    }
    swprintf_s(text, L"%.1f ms", seconds * 1000.0);
    return text;
}

// A speed in multiples of real time, such as 0.01x, 1x, 12.5x, 604800x or 7.9e13x.
inline std::wstring format_speed(double multiple)
{
    wchar_t text[64];
    if (!(multiple > 0.0))
    {
        swprintf_s(text, L"0x");
    }
    else if (multiple >= 1e6)
    {
        // 7.9e+13 -> 7.9e13
        swprintf_s(text, L"%.3g", multiple);
        std::wstring s = text;
        const auto e = s.find(L'e');
        if (e != std::wstring::npos)
        {
            size_t digits = e + 2;
            while (digits + 1 < s.size() && s[digits] == L'0')
                ++digits;
            s = s.substr(0, e) + L"e" + s.substr(digits);
        }
        return s + L"x";
    }
    else if (multiple >= 999.5)
    {
        swprintf_s(text, L"%.0fx", multiple);
    }
    else
    {
        swprintf_s(text, L"%.3gx", multiple);
    }
    return text;
}
