#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <cstdint>

namespace PhotoManager::Watch {

enum class ChangeType {
    FILE_ADDED,
    FILE_REMOVED,
    FILE_MODIFIED,
    FILE_RENAMED_FROM,
    FILE_RENAMED_TO,
    DIRECTORY_ADDED,
    DIRECTORY_REMOVED
};

struct FileChangeEvent {
    ChangeType   type;
    std::wstring path;
    std::wstring old_path;
    DWORD        timestamp_ms;
};

using ChangeCallback = std::function<void(const FileChangeEvent&)>;

class FolderWatcher {
public:
    explicit FolderWatcher(ChangeCallback on_change = nullptr);
    ~FolderWatcher();

    bool  startWatching(const std::wstring& folder_path,
                         bool recursive = false,
                         bool photos_only = true);
    bool  addWatch(const std::wstring& folder_path,
                    bool recursive = false,
                    bool photos_only = true);
    void  stopWatching();
    bool  isRunning() const;

    std::wstring watchedFolder() const;

    std::vector<FileChangeEvent> pollEvents();
    void setCallback(ChangeCallback cb);

    void setDebounceMs(uint32_t ms);

private:
    struct WatchEntry {
        std::wstring path;
        HANDLE       dir_handle;
        HANDLE       event_handle;
        bool         recursive;
        bool         photos_only;
        uint8_t      buffer[65536];
        OVERLAPPED   overlapped;
    };

    void watchThread();
    void reissueRead(WatchEntry& entry);
    void processNotifications(WatchEntry& entry);
    bool isPhotoExtension(const std::wstring& path) const;
    void pushEvent(FileChangeEvent ev);

    std::vector<WatchEntry>  watchers_;
    std::atomic<bool>        running_{false};
    std::thread              worker_;
    HANDLE                   stop_event_{INVALID_HANDLE_VALUE};

    ChangeCallback           callback_;
    std::mutex               queue_mutex_;
    std::queue<FileChangeEvent> event_queue_;
    std::condition_variable  cv_;

    uint32_t                 debounce_ms_{100};
    std::wstring             primary_folder_;
};

} // namespace PhotoManager::Watch