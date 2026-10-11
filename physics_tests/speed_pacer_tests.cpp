#include "CppUnitTest.h"
#include "speed_pacer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(speed_pacer_tests)
{
public:
    TEST_METHOD(real_time_runs_one_second_of_steps_per_second)
    {
        speed_pacer pacer;
        size_t total = 0;
        for (int frame = 0; frame < 60; ++frame)
            total += pacer.steps_for(1.0, 0.01, 1.0 / 60.0);
        // One simulated second at dt = 0.01 is 100 steps (allow one for floating-point rounding).
        Assert::IsTrue(total >= 99 && total <= 100);
    }

    TEST_METHOD(speed_scales_the_number_of_steps)
    {
        for (const double speed : { 0.01, 0.1, 1.0, 10.0, 100.0, 1000.0 })
        {
            speed_pacer pacer;
            const double dt = 0.001;
            size_t total = 0;
            for (int frame = 0; frame < 600; ++frame)   // ten seconds of 60 fps frames
                total += pacer.steps_for(speed, dt, 1.0 / 60.0);

            const double expected = speed * 10.0 / dt;
            Assert::IsTrue(std::abs(double(total) - expected) <= 1.0 + 1e-9 * expected);
        }
    }

    TEST_METHOD(slow_speeds_carry_the_fraction_of_a_step_between_frames)
    {
        speed_pacer pacer;
        // 0.01x with dt = 1 needs 0.01 s of simulated time per wall second: a step every 100 seconds.
        size_t first = 0;
        for (int i = 0; i < 99; ++i)
            first += pacer.steps_for(0.01, 1.0, 1.0);
        Assert::AreEqual(size_t(0), first);
        Assert::AreEqual(size_t(1), pacer.steps_for(0.01, 1.0, 1.0));
    }

    TEST_METHOD(a_reset_forgets_the_owed_time)
    {
        speed_pacer pacer;
        Assert::AreEqual(size_t(0), pacer.steps_for(1.0, 1.0, 0.9));
        pacer.reset();
        Assert::AreEqual(size_t(0), pacer.steps_for(1.0, 1.0, 0.2));
    }

    TEST_METHOD(invalid_inputs_give_no_steps)
    {
        speed_pacer pacer;
        Assert::AreEqual(size_t(0), pacer.steps_for(0.0, 0.1, 1.0));
        Assert::AreEqual(size_t(0), pacer.steps_for(-1.0, 0.1, 1.0));
        Assert::AreEqual(size_t(0), pacer.steps_for(1.0, 0.0, 1.0));
        Assert::AreEqual(size_t(0), pacer.steps_for(1.0, 0.1, 0.0));
    }
};
