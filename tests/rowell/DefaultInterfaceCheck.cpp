#include "net_host.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    using namespace std::chrono_literals;
    double worstMs = 0;
    NetHost::Interface last{};
    for (int sample = 0; sample < 45; ++sample) {
        const auto begin = std::chrono::steady_clock::now();
        last = NetHost::DefaultInterface();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        if (ms > worstMs)
            worstMs = ms;
        std::this_thread::sleep_for(100ms);
    }
    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &last.ip, ip, sizeof(ip));
    std::printf("DEFAULT_INTERFACE_CHECK: 45 production calls, worst %.3f ms, address %s\n", worstMs, ip);
    return worstMs < 100.0 && last.ip != 0 ? 0 : 1;
}
