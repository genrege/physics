#pragma once

#include <cmath>
#include <cstddef>

// Decides how many fixed-size steps to run for a slice of wall-clock time so the simulation advances at a
// chosen multiple of real time. The fraction of a step left over is carried to the next call, so slow speeds
// (less than one step per frame) still advance at the right average rate.
class speed_pacer
{
public:
    // `speed` is simulated seconds per wall-clock second; `dt` is the step size in simulated seconds.
    size_t steps_for(double speed, double dt, double wall_seconds)
    {
        if (!(speed > 0.0) || !(dt > 0.0) || !(wall_seconds > 0.0))
            return 0;

        carry_ += speed * wall_seconds;
        const double whole = std::floor(carry_ / dt);
        if (whole < 1.0)
            return 0;

        carry_ -= whole * dt;
        return static_cast<size_t>(whole);
    }

    void reset() { carry_ = 0.0; }

private:
    double carry_ = 0.0;   // simulated seconds owed but not yet a whole step
};
