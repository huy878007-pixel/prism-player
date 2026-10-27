// Background Windows Shell thumbnails for the in-app file browser.
#pragma once

#include "common.h"

#include <unordered_set>

struct ThumbResult {
    std::wstring path;
    std::wstring key;  // full path + file size + mtime
    int width = 0;
    int height = 0;
    // Top-down premultiplied BGRA, or null when the shell has no image.
    std::shared_ptr<const std::vector<uint8_t>> bgra;
};

// One worker thread owns every IShellItemImageFactory call, so the UI thread
// only ever posts paths and polls finished pixels.
class ThumbnailCache {
public:
    ThumbnailCache() = default;
    ~ThumbnailCache();
    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    // Replaces the wanted set and queues the paths not asked for yet. The
    // call itself never waits for the shell.
    void request(const std::vector<std::wstring>& paths);
    // Folder change / browser close: drop queued work and ignore in-flight
    // jobs. The result cache survives so reopening the folder is instant.
    void cancel();
    // Finished results since the previous poll. UI thread only.
    std::vector<ThumbResult> poll();
    void stop();

private:
    struct Entry {
        std::wstring key;
        int width = 0;
        int height = 0;
        std::shared_ptr<const std::vector<uint8_t>> bgra;
    };
    struct Job {
        std::wstring path;
        uint64_t gen = 0;
    };
    void ensureStartedLocked();
    void worker();

    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::unordered_map<std::wstring, uint64_t> wanted_;
    std::unordered_set<std::wstring> asked_;
    std::unordered_map<std::wstring, Entry> cache_;
    std::deque<std::wstring> cacheOrder_;
    std::vector<ThumbResult> done_;
    uint64_t gen_ = 0;
    int inFlight_ = 0;
    bool quit_ = false;
    bool started_ = false;
    std::thread thread_;
};
