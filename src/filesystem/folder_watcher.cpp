#include "folder_watcher.hpp"
#include "file_ops.hpp"
#include <algorithm>
#include <stdexcept>

namespace PhotoManager::Watch {

namespace {

static const std::vector<std::wstring> PHOTO_EXT = {
    L".jpg",L".jpeg",L".png",L".tiff",L".tif",
    L".raw",L".cr2",L".cr3",L".nef",L".arw",
    L".orf",L".rw2",L".dng",L".heic",L".heif",
    L".bmp",L".webp",L".psd"
};

[[maybe_unused]] const wchar_t* changeTypeName(ChangeType t) {
    switch (t) {
    case ChangeType::FILE_ADDED:       return L"ADDED";
    case ChangeType::FILE_REMOVED:     return L"REMOVED";
    case ChangeType::FILE_MODIFIED:    return L"MODIFIED";
    case ChangeType::FILE_RENAMED_FROM:return L"RENAMED_FROM";
    case ChangeType::FILE_RENAMED_TO:  return L"RENAMED_TO";
    case ChangeType::DIRECTORY_ADDED:  return L"DIR_ADDED";
    case ChangeType::DIRECTORY_REMOVED:return L"DIR_REMOVED";
    default:                            return L"UNKNOWN";
    }
}

} // anonymous namespace
FolderWatcher::FolderWatcher(ChangeCallback on_change)
    : callback_(std::move(on_change))
{
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

FolderWatcher::~FolderWatcher() {
    stopWatching();
    if (stop_event_ != INVALID_HANDLE_VALUE)
        CloseHandle(stop_event_);
}

bool FolderWatcher::isPhotoExtension(const std::wstring& path) const {
    size_t dot = path.rfind(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) c = towlower(c);
    for (auto& e : PHOTO_EXT) if (ext == e) return true;
    return false;
}

void FolderWatcher::pushEvent(FileChangeEvent ev) {
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        event_queue_.push(ev);
    }
    cv_.notify_all();
    if (callback_) callback_(ev);
}

bool FolderWatcher::startWatching(const std::wstring& folder_path,
                                    bool recursive, bool photos_only)
{
    if (running_) stopWatching();
    primary_folder_ = folder_path;
    watchers_.clear();

    if (!addWatch(folder_path, recursive, photos_only)) return false;

    ResetEvent(stop_event_);
    running_ = true;
    worker_ = std::thread(&FolderWatcher::watchThread, this);
    return true;
}

bool FolderWatcher::addWatch(const std::wstring& folder_path,
                               bool recursive, bool photos_only)
{
    HANDLE dir = CreateFileW(folder_path.c_str(),
                              FILE_LIST_DIRECTORY,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                              nullptr);
    if (dir == INVALID_HANDLE_VALUE) return false;

    WatchEntry entry{};
    entry.path        = folder_path;
    entry.dir_handle  = dir;
    entry.recursive   = recursive;
    entry.photos_only = photos_only;

    entry.event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (entry.event_handle == INVALID_HANDLE_VALUE) {
        CloseHandle(dir);
        return false;
    }

    entry.overlapped.hEvent = entry.event_handle;
    watchers_.push_back(std::move(entry));
    reissueRead(watchers_.back());
    return true;
}

void FolderWatcher::reissueRead(WatchEntry& e) {
    DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME  |
                   FILE_NOTIFY_CHANGE_DIR_NAME   |
                   FILE_NOTIFY_CHANGE_SIZE       |
                   FILE_NOTIFY_CHANGE_LAST_WRITE |
                   FILE_NOTIFY_CHANGE_CREATION;

    ReadDirectoryChangesW(e.dir_handle, e.buffer, sizeof(e.buffer),
                           e.recursive, filter, nullptr,
                           &e.overlapped, nullptr);
}

void FolderWatcher::processNotifications(WatchEntry& entry) {
    DWORD bytes;
    if (!GetOverlappedResult(entry.dir_handle, &entry.overlapped, &bytes, FALSE))
        return;

    std::wstring pending_rename_from;
    const uint8_t* ptr = entry.buffer;

    while (ptr < entry.buffer + bytes) {
        auto* fni = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(ptr);

        std::wstring fname(fni->FileName, fni->FileNameLength / sizeof(wchar_t));
        std::wstring full_path = entry.path;
        if (full_path.back() != L'\\') full_path += L'\\';
        full_path += fname;

        bool is_dir = (GetFileAttributesW(full_path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (!entry.photos_only || is_dir || isPhotoExtension(fname)) {
            FileChangeEvent ev{};
            ev.timestamp_ms = GetTickCount();
            ev.path = full_path;

            switch (fni->Action) {
            case FILE_ACTION_ADDED:
                ev.type = is_dir ? ChangeType::DIRECTORY_ADDED : ChangeType::FILE_ADDED;
                pushEvent(ev);
                break;
            case FILE_ACTION_REMOVED:
                ev.type = is_dir ? ChangeType::DIRECTORY_REMOVED : ChangeType::FILE_REMOVED;
                pushEvent(ev);
                break;
            case FILE_ACTION_MODIFIED:
                ev.type = ChangeType::FILE_MODIFIED;
                pushEvent(ev);
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                pending_rename_from = full_path;
                break;
            case FILE_ACTION_RENAMED_NEW_NAME:
                {
                    FileChangeEvent rfrom{};
                    rfrom.type = ChangeType::FILE_RENAMED_FROM;
                    rfrom.path = pending_rename_from;
                    rfrom.timestamp_ms = ev.timestamp_ms;
                    pushEvent(rfrom);

                    FileChangeEvent rto{};
                    rto.type     = ChangeType::FILE_RENAMED_TO;
                    rto.path     = full_path;
                    rto.old_path = pending_rename_from;
                    rto.timestamp_ms = ev.timestamp_ms;
                    pushEvent(rto);
                    pending_rename_from.clear();
                }
                break;
            }
        }

        if (!fni->NextEntryOffset) break;
        ptr += fni->NextEntryOffset;
    }

    reissueRead(entry);
}

void FolderWatcher::watchThread() {
    std::vector<HANDLE> events;
    events.push_back(stop_event_);
    for (auto& w : watchers_) events.push_back(w.event_handle);

    while (running_) {
        DWORD count = static_cast<DWORD>(events.size());
        DWORD wait = WaitForMultipleObjectsEx(count, events.data(),
                                               FALSE, INFINITE, FALSE);
        if (!running_) break;

        if (wait == WAIT_OBJECT_0) {
            break;
        } else if (wait >= WAIT_OBJECT_0 + 1 &&
                   wait < WAIT_OBJECT_0 + count)
        {
            size_t idx = wait - WAIT_OBJECT_0 - 1;
            if (idx < watchers_.size()) {
                processNotifications(watchers_[idx]);
            }
        }

        if (debounce_ms_ > 0) Sleep(debounce_ms_);
    }

    for (auto& w : watchers_) {
        CancelIo(w.dir_handle);
        CloseHandle(w.dir_handle);
        CloseHandle(w.event_handle);
    }
    watchers_.clear();
}

void FolderWatcher::stopWatching() {
    if (!running_) return;
    running_ = false;
    SetEvent(stop_event_);
    if (worker_.joinable()) worker_.join();
}

bool FolderWatcher::isRunning() const { return running_; }

std::wstring FolderWatcher::watchedFolder() const { return primary_folder_; }

std::vector<FileChangeEvent> FolderWatcher::pollEvents() {
    std::lock_guard<std::mutex> lk(queue_mutex_);
    std::vector<FileChangeEvent> result;
    while (!event_queue_.empty()) {
        result.push_back(event_queue_.front());
        event_queue_.pop();
    }
    return result;
}

void FolderWatcher::setCallback(ChangeCallback cb) {
    callback_ = std::move(cb);
}

void FolderWatcher::setDebounceMs(uint32_t ms) {
    debounce_ms_ = ms;
}

} // namespace PhotoManager::Watch