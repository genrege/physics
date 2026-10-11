#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// Runs step() on a worker thread and paces it against a consumer via present()/frame_done().
// The worker never blocks indefinitely on the consumer, so stop() cannot deadlock with a
// consumer thread that is itself blocked in stop().
class sim_runner
{
public:
    // Returns true if the state changed and a frame should be presented.
    using step_fn = std::function<bool()>;
    // Requests a repaint; returns false if the request could not be queued.
    using present_fn = std::function<bool()>;

    sim_runner(step_fn step, present_fn present, std::chrono::milliseconds frame_timeout = std::chrono::milliseconds(100))
        : step_(std::move(step)), present_(std::move(present)), frame_timeout_(frame_timeout)
    {
    }

    sim_runner(const sim_runner&) = delete;
    sim_runner& operator=(const sim_runner&) = delete;

    ~sim_runner() { stop(); }

    void start()
    {
        if (thread_.joinable())
            return;
        stop_ = false;
        thread_ = std::thread([this] { run(); });
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable())
            thread_.join();
    }

    // Called by the consumer once it has finished presenting a frame.
    void frame_done()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            frame_done_ = true;
        }
        cv_.notify_all();
    }

    bool running() const { return thread_.joinable(); }

private:
    void run()
    {
        while (!stop_)
        {
            if (!step_())
                continue;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                frame_done_ = false;
            }

            if (present_())
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, frame_timeout_, [this] { return stop_.load() || frame_done_; });
            }
        }
    }

    step_fn step_;
    present_fn present_;
    std::chrono::milliseconds frame_timeout_;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{ false };
    bool frame_done_ = false;
};
