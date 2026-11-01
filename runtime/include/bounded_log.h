#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#ifdef _WIN32
#include <io.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace BoundedLog {
inline constexpr size_t kActivityBytes = 2 * 1024 * 1024;
inline constexpr size_t kCrashBytes = 256 * 1024;

inline bool Seek(FILE* file, int64_t offset, int origin) {
#ifdef _WIN32
    return _fseeki64(file, offset, origin) == 0;
#else
    return fseeko(file, offset, origin) == 0;
#endif
}

inline int64_t Tell(FILE* file) {
#ifdef _WIN32
    return _ftelli64(file);
#else
    return ftello(file);
#endif
}

// Bring logs from older, uncapped builds within the limit, preserving the tail.
// Uses a fixed buffer and edits only the exact log file, without a temp copy.
inline bool TrimExisting(const std::string& path, size_t limit) {
    FILE* file = std::fopen(path.c_str(), "r+b");
    if (!file)
        return errno == ENOENT;
    bool ok = Seek(file, 0, SEEK_END);
    const int64_t bytes = ok ? Tell(file) : -1;
    ok = bytes >= 0;
    if (ok && uint64_t(bytes) > limit) {
        char buffer[4096];
        size_t copied = 0;
        const int64_t start = bytes - int64_t(limit);
        while (ok && copied < limit) {
            const size_t count = std::min(sizeof(buffer), limit - copied);
            ok = Seek(file, start + int64_t(copied), SEEK_SET) &&
                 std::fread(buffer, 1, count, file) == count &&
                 Seek(file, int64_t(copied), SEEK_SET) &&
                 std::fwrite(buffer, 1, count, file) == count;
            copied += count;
        }
        ok = ok && std::fflush(file) == 0;
        if (ok) {
#ifdef _WIN32
            ok = _chsize_s(_fileno(file), limit) == 0;
#else
            ok = ftruncate(fileno(file), off_t(limit)) == 0;
#endif
        }
    }
    return std::fclose(file) == 0 && ok;
}

// One writer owns a stream. Rotation happens only at the byte limit, not per
// frame; ordinary writes use a byte counter rather than filesystem scans.
class File {
    std::string path_;
    std::string previous_;
    FILE* file_ = nullptr;
    size_t limit_ = 0;
    size_t bytes_ = 0;

    bool Rotate() {
        Close();
#ifdef _WIN32
        if (!MoveFileExA(path_.c_str(), previous_.c_str(), MOVEFILE_REPLACE_EXISTING))
            return false;
#else
        if (std::rename(path_.c_str(), previous_.c_str()) != 0)
            return false;
#endif
        file_ = std::fopen(path_.c_str(), "ab");
        bytes_ = 0;
        return file_ != nullptr;
    }

public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { Close(); }

    void Close() {
        if (file_)
            std::fclose(file_);
        file_ = nullptr;
    }

    bool Open(const std::string& path, size_t limit) {
        Close();
        if (!limit)
            return false;
        path_ = path;
        previous_ = path + ".previous";
        limit_ = limit;
        if (!TrimExisting(previous_, limit_) || !TrimExisting(path_, limit_))
            return false;
        file_ = std::fopen(path_.c_str(), "ab");
        if (!file_ || !Seek(file_, 0, SEEK_END)) {
            Close();
            return false;
        }
        const int64_t bytes = Tell(file_);
        if (bytes < 0 || uint64_t(bytes) > limit_) {
            Close();
            return false;
        }
        bytes_ = size_t(bytes);
        return true;
    }

    bool Append(std::string_view text) {
        if (!file_)
            return false;
        if (text.size() > limit_)
            text.remove_prefix(text.size() - limit_);
        if (text.empty())
            return true;
        if (text.size() > limit_ - bytes_ && !Rotate())
            return false;
        if (std::fwrite(text.data(), 1, text.size(), file_) != text.size() ||
            std::fflush(file_) != 0) {
            Close();
            return false;
        }
        bytes_ += text.size();
        return true;
    }
};

inline bool AppendReport(const std::string& path, std::string_view text) {
    File log;
    return log.Open(path, kCrashBytes) && log.Append(text);
}

#ifndef _WIN32
// Fatal signal fallback: no allocation, stdio, locks or filesystem C++ calls.
// Legacy oversized fallback files are discarded; the latest report survives.
// Ordinary game/crash logs use File above, which preserves oversized tails.
inline bool AppendEmergency(const char* path, const char* previous,
                            const char* text, size_t count, size_t limit = kCrashBytes) {
    const int savedErrno = errno;
    bool ok = false;
    if (limit && count) {
        if (count > limit) {
            text += count - limit;
            count = limit;
        }
        int old = open(previous, O_WRONLY | O_CLOEXEC);
        if (old >= 0) {
            struct stat state{};
            if (fstat(old, &state) == 0 && state.st_size > off_t(limit) && ftruncate(old, 0) != 0) {
                close(old);
                errno = savedErrno;
                return false;
            }
            close(old);
        }
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (fd >= 0) {
            struct stat state{};
            if (fstat(fd, &state) == 0) {
                bool ready = true;
                if (state.st_size > off_t(limit)) {
                    ready = ftruncate(fd, 0) == 0;
                } else if (state.st_size > off_t(limit - count)) {
                    close(fd);
                    fd = -1;
                    ready = rename(path, previous) == 0;
                    if (ready) {
                        fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
                        ready = fd >= 0;
                    }
                }
                if (ready) {
                    size_t written = 0;
                    while (written < count) {
                        const ssize_t n = write(fd, text + written, count - written);
                        if (n < 0 && errno == EINTR) continue;
                        if (n <= 0) break;
                        written += size_t(n);
                    }
                    ok = written == count;
                }
            }
            if (fd >= 0) close(fd);
        }
    }
    errno = savedErrno;
    return ok;
}
#endif
} // namespace BoundedLog
