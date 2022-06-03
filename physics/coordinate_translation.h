#pragma once

#include "framework.h"

class viewport
{
public:
    viewport(const RECT& screen_size, double xmin, double xmax, double ymin, double ymax)
        : screen_size_(screen_size), xmin_(xmin), xmax_(xmax), ymin_(ymin), ymax_(ymax)
    {

    }

    int x_to_screen(double x)
    {
        const auto scale = double((screen_size_.right - screen_size_.left) / (xmax_ - xmin_));

        return int((x - xmin_) * scale);
    }
    int y_to_screen(double y)
    {
        const auto scale = double((screen_size_.bottom - screen_size_.top) / (ymax_ - ymin_));

        return int((y - ymin_) * scale);
    }

private:
    RECT screen_size_;
    double xmin_;
    double xmax_;
    double ymin_;
    double ymax_;
};