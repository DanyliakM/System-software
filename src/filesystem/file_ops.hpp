#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <optional>
#include <ctime>
#include <functional>
#include <cstdint>

namespace PhotoManager::FileSystem {

static const std::vector<std::wstring> PHOTO_EXTENSIONS = {
    L".jpg", L".jpeg", L".png", L".tiff", L".tif",
    L".raw", L".cr2", L".cr3", L".nef", L".arw",
    L".orf", L".rw2", L".dng", L".heic", L".heif",
    L".bmp", L".webp", L".psd"
};

struct FileInfo {
    std::wstring full_path;
    std::wstring name;
    std::wstring stem;
    std::wstring extension;
    uint64_t     size_bytes;
    std::tm      date_created;
    std::tm      date_modified;
    bool         is_directory;
};

struct DriveInfo {
    std::wstring letter;
    std::wstring label;
    std::wstring filesystem;
    uint64_t     total_bytes;
    uint64_t     free_bytes;
    UINT         drive_type;
};

struct CopyProgress {
    uint64_t bytes_transferred;
    uint64_t total_bytes;
    std::wstring current_file;
};

using ProgressCallback = std::function<void(const CopyProgress&)>;

bool                     IsPhotoFile(const std::wstring& path);
std::tm                  FileTimeToTm(const FILETIME& ft);
FILETIME                 TmToFileTime(const std::tm& t);
std::string              WideToUtf8(const std::wstring& wide);
std::wstring             Utf8ToWide(const std::string& utf8);

std::vector<FileInfo>    ScanDirectory(const std::wstring& path, bool recursive, bool photos_only = true);
std::optional<FileInfo>  GetFileInfo(const std::wstring& path);
bool                     FileExists(const std::wstring& path);
bool                     DirectoryExists(const std::wstring& path);

bool CopyPhoto(const std::wstring& src, const std::wstring& dst,
bool overwrite = false, ProgressCallback cb = nullptr);
bool MovePhoto(const std::wstring& src, const std::wstring& dst);
bool DeletePhoto(const std::wstring& path, bool to_recycle_bin = true);
bool CreateDirectoryPath(const std::wstring& path);

std::vector<DriveInfo>   EnumerateDrives();
bool                     OpenWithDefaultApp(const std::wstring& path);
bool                     RevealInExplorer(const std::wstring& path);
std::wstring             GetTempDirectory();
std::wstring             SanitizeFileName(const std::wstring& name);
std::wstring             MakeUniquePath(const std::wstring& desired_path);

} // namespace PhotoManager::FileSystem