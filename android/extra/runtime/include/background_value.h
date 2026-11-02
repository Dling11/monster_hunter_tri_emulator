#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

// Readers only copy a snapshot. A slow refresh never holds the reader's mutex.
template <class T>
class BackgroundValue {
    mutable std::mutex valueMutex_;
    T value_;
    std::function<T()> refresh_;
    std::chrono::milliseconds period_;
    std::mutex waitMutex_;
    std::condition_variable_any wake_;
    // Last member: stopped/joined before the data used by the worker is destroyed.
    std::jthread worker_;

    void Run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            try {
                T next = refresh_();
                std::lock_guard guard(valueMutex_);
                value_ = std::move(next);
            } catch (...) {
                // Keep the last usable snapshot when a refresh cannot finish.
            }
            std::unique_lock guard(waitMutex_);
            wake_.wait_for(guard, stop, period_, [] { return false; });
        }
    }

public:
    BackgroundValue(T initial, std::function<T()> refresh, std::chrono::milliseconds period)
        : value_(std::move(initial)), refresh_(std::move(refresh)), period_(period),
          worker_([this](std::stop_token stop) { Run(stop); }) {}

    BackgroundValue(const BackgroundValue&) = delete;
    BackgroundValue& operator=(const BackgroundValue&) = delete;

    T Get() const {
        std::lock_guard guard(valueMutex_);
        return value_;
    }
};
