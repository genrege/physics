#include "CppUnitTest.h"
#include "sim_runner.h"

#include <atomic>
#include <future>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(sim_runner_tests)
{
public:
    // Regression for the UI deadlock: the worker used to SendMessage to a UI thread that was blocked joining it.
    TEST_METHOD(stop_returns_when_consumer_never_acknowledges_frames)
    {
        std::atomic<int> steps{ 0 };
        sim_runner runner([&] { ++steps; }, [] { return true; }, std::chrono::milliseconds(10000));
        runner.start();
        while (steps < 1)
            std::this_thread::yield();

        auto stopped = std::async(std::launch::async, [&] { runner.stop(); });
        Assert::IsTrue(stopped.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    }

    TEST_METHOD(stop_returns_while_step_is_slow)
    {
        std::atomic<bool> in_step{ false };
        sim_runner runner([&] { in_step = true; std::this_thread::sleep_for(std::chrono::milliseconds(100)); }, [] { return false; });
        runner.start();
        while (!in_step)
            std::this_thread::yield();

        auto stopped = std::async(std::launch::async, [&] { runner.stop(); });
        Assert::IsTrue(stopped.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    }

    TEST_METHOD(frame_done_paces_the_worker)
    {
        std::atomic<int> steps{ 0 };
        sim_runner runner([&] { ++steps; }, [] { return true; }, std::chrono::milliseconds(10000));
        runner.start();
        while (steps < 1)
            std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Assert::AreEqual(1, steps.load());

        runner.frame_done();
        while (steps < 2)
            std::this_thread::yield();
        runner.stop();
    }

    TEST_METHOD(rapid_start_stop_cycles_do_not_hang)
    {
        std::atomic<int> steps{ 0 };
        sim_runner runner([&] { ++steps; }, [] { return true; }, std::chrono::milliseconds(10000));

        auto cycles = std::async(std::launch::async, [&] {
            for (int i = 0; i < 1000; ++i)
            {
                runner.start();
                runner.stop();
            }
            });
        Assert::IsTrue(cycles.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
    }
};
