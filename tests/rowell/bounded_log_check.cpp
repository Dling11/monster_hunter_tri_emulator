#include "bounded_log.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifndef _WIN32
#include <csignal>
#endif

namespace fs = std::filesystem;
static int checks = 0;
static void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
static std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static void Write(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    out << data;
    Check(bool(out), "fixture write failed");
}
#ifndef _WIN32
static std::string signalPath, signalPrevious;
static volatile sig_atomic_t signalResult = 0;
static void SignalReport(int) {
    const char text[] = "signal crash report\n";
    signalResult = BoundedLog::AppendEmergency(signalPath.c_str(), signalPrevious.c_str(), text, sizeof(text) - 1, 64);
}
#endif

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("isolated fixture path required");
        const fs::path root = argv[1];
        fs::create_directories(root / "nand");
        Write(root / "nand/save.bin", "character progress\0unchanged");
        const std::string save = Read(root / "nand/save.bin");
        std::string blocks(8192 + 17, '\0');
        for (size_t i = 0; i < blocks.size(); ++i) blocks[i] = char(i % 251);
        const std::string blockPath = (root / "large-legacy.log").string();
        Write(blockPath, blocks);
        Check(BoundedLog::TrimExisting(blockPath, 8192), "multi-buffer legacy trim failed");
        Check(Read(blockPath) == blocks.substr(17), "overlapping multi-buffer trim corrupted tail");
        const std::string path = (root / "game_log.txt").string();
        const std::string previous = path + ".previous";
        const std::string old = "old-prefix" + std::string(200, 'p') + "previous-tail";
        const std::string current = "old-prefix" + std::string(200, 'c') + "current-tail";
        Write(previous, old);
        Write(path, current);
        {
            BoundedLog::File log;
            Check(log.Open(path, 64), "open legacy logs");
            Check(Read(path) == current.substr(current.size() - 64), "legacy current tail lost");
            Check(Read(previous) == old.substr(old.size() - 64), "legacy previous tail lost");
            Check(log.Append("latest\n"), "first rotated append");
            Check(Read(previous) == current.substr(current.size() - 64), "rotation did not preserve recent log");
            Check(Read(path) == "latest\n", "latest record lost");
            for (int n = 0; n < 200; ++n) {
                Check(log.Append("row " + std::to_string(n) + "\n"), "repeated append");
                Check(fs::file_size(path) <= 64 && fs::file_size(previous) <= 64, "rotation exceeded cap");
            }
            const std::string huge = std::string(300, 'z') + "newest-end";
            Check(log.Append(huge), "oversized record failed");
            Check(Read(path) == huge.substr(huge.size() - 64), "oversized record did not retain tail");
            Check(log.Append({}), "empty append failed");
        }
        {
            BoundedLog::File log;
            Check(log.Open(path, 64) && log.Append("after restart\n"), "reopen lost logging");
            Check(Read(path) == "after restart\n", "reopen did not preserve newest report");
        }
        const std::string crash = (root / "crash.txt").string();
        Write(crash, std::string(BoundedLog::kCrashBytes, 'a'));
        Check(BoundedLog::AppendReport(crash, "latest crash\n"), "crash rotation failed");
        Check(Read(crash) == "latest crash\n", "latest crash missing");
        Check(fs::file_size(crash + ".previous") == BoundedLog::kCrashBytes, "previous crash missing");
        {
            BoundedLog::File log;
            Check(!log.Open((root / "missing-parent/log.txt").string(), 64), "missing parent unexpectedly opened");
            Check(!log.Append("still cannot write"), "failed logger accepted writes");
            Check(!log.Open(path, 0), "zero cap accepted");
            const std::string blocked = (root / "blocked.txt").string();
            Write(blocked, std::string(64, 'b'));
            Check(log.Open(blocked, 64), "blocked fixture open");
            fs::create_directory(blocked + ".previous");
            Check(!log.Append("overflow"), "rotation into directory should fail");
            Check(fs::is_directory(blocked + ".previous"), "logger removed unrelated directory");
            Check(fs::file_size(blocked) == 64, "failed rotation grew current log");
        }
#ifndef _WIN32
        signalPath = (root / "emergency.txt").string();
        signalPrevious = signalPath + ".previous";
        Write(signalPath, std::string(64, 'e'));
        Write(signalPrevious, std::string(300, 'p'));
        std::signal(SIGUSR1, SignalReport);
        std::raise(SIGUSR1);
        Check(signalResult == 1 && Read(signalPath) == "signal crash report\n", "signal fallback failed");
        Check(fs::file_size(signalPrevious) == 64, "signal fallback cap failed");
        Write(signalPath, std::string(300, 'e'));
        errno = ERANGE;
        Check(BoundedLog::AppendEmergency(signalPath.c_str(), signalPrevious.c_str(), "new", 3, 64), "legacy emergency append");
        Check(errno == ERANGE, "signal logger changed errno");
        Check(Read(signalPath) == "new", "legacy emergency cap failed");
#endif
        Check(Read(root / "nand/save.bin") == save, "log handling altered saved progress");
        Check(BoundedLog::kActivityBytes == 2097152 && BoundedLog::kCrashBytes == 262144, "production limits changed");
        std::cout << "Bounded native logs passed: " << checks << " checks.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
