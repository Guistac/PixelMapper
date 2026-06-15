#pragma once
#include <filesystem>
#include <string>

namespace PixelMapper {

/// Lightweight file-change detector using std::filesystem::last_write_time.
/// Call check() periodically; it returns true (and resets the baseline) when
/// the file on disk is newer than what was last seen.
struct FileWatcher {
    std::string path;
    std::filesystem::file_time_type lastWriteTime{};

    explicit FileWatcher(const std::string& p = "") : path(p) {
        reset();
    }

    void setPath(const std::string& p) {
        path = p;
        reset();
    }

    /// Returns true if the file has changed since the last call to reset() or
    /// the last time check() returned true.
    bool check() {
        if (path.empty()) return false;
        try {
            if (!std::filesystem::exists(path)) return false;
            auto t = std::filesystem::last_write_time(path);
            if (t != lastWriteTime) {
                lastWriteTime = t;
                return true;
            }
        } catch (...) {}
        return false;
    }

    /// Reset baseline to current write time (so the next check() won't fire
    /// unless the file changes again).
    void reset() {
        if (path.empty()) return;
        try {
            if (std::filesystem::exists(path))
                lastWriteTime = std::filesystem::last_write_time(path);
        } catch (...) {}
    }
};

} // namespace PixelMapper
