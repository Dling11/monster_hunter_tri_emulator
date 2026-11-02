#include "background_value.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <stdexcept>

using namespace std::chrono_literals;

static void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "BACKGROUND_VALUE_CHECK failed: %s\n", message);
        std::_Exit(1);
    }
}

int main() {
    std::promise<void> entered, release;
    auto started = entered.get_future();
    auto gate = release.get_future().share();
    {
        BackgroundValue<int> cache(11, [&] {
            entered.set_value();
            gate.wait();
            return 42;
        }, 1h);
        Check(started.wait_for(2s) == std::future_status::ready, "refresh worker starts");
        const auto begin = std::chrono::steady_clock::now();
        for (int i = 0; i < 10000; ++i)
            Check(cache.Get() == 11, "initial route remains readable while lookup is blocked");
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        Check(elapsed < 200ms, "blocked lookup does not block frame-thread readers");
        release.set_value();
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (cache.Get() != 42 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        Check(cache.Get() == 42, "completed lookup publishes the new route");
    }
    std::atomic<int> attempts{0};
    const auto begin = std::chrono::steady_clock::now();
    {
        BackgroundValue<int> cache(3, [&] {
            if (++attempts > 1)
                throw std::runtime_error("simulated interface refresh failure");
            return 7;
        }, 20ms);
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (attempts < 3 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(1ms);
        Check(attempts >= 3, "background refresh continues");
        Check(cache.Get() == 7, "failed refresh preserves last usable route");
    }
    Check(std::chrono::steady_clock::now() - begin < 1s, "shutdown wakes the refresh timer");
    std::puts("BACKGROUND_VALUE_CHECK: blocked-reader, publication, failure and shutdown checks passed");
}
