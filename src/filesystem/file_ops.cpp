#include "file_ops.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <stdexcept>
#include <cassert>
#include <cstdint>

namespace PhotoManager::FileSystem {

bool IsPhotoFile(const std::wstring& path) {
    std::wstring ext;
    size_t dot = path.rfind(L'.');
    if (dot == std::wstring::npos) return false;
    ext = path.substr(dot);
    for (auto& c : ext) c = towlower(c);
    for (const auto& e : PHOTO_EXTENSIONS)
        if (ext == e) return true;
    return false;
}

std::tm FileTimeToTm(const FILETIME& ft) {
    FILETIME local;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &local);
    FileTimeToSystemTime(&local, &st);
    std::tm t{};
    t.tm_year  = st.wYear  - 1900;
    t.tm_mon   = st.wMonth - 1;
    t.tm_mday  = st.wDay;
    t.tm_hour  = st.wHour;
    t.tm_min   = st.wMinute;
    t.tm_sec   = st.wSecond;
    t.tm_wday  = st.wDayOfWeek;
    return t;
}

FILETIME TmToFileTime(const std::tm& t) {
    SYSTEMTIME st{};
    st.wYear   = static_cast<WORD>(t.tm_year + 1900);
    st.wMonth  = static_cast<WORD>(t.tm_mon  + 1);
    st.wDay    = static_cast<WORD>(t.tm_mday);
    st.wHour   = static_cast<WORD>(t.tm_hour);
    st.wMinute = static_cast<WORD>(t.tm_min);
    st.wSecond = static_cast<WORD>(t.tm_sec);
    FILETIME local, utc;
    SystemTimeToFileTime(&st, &local);
    LocalFileTimeToFileTime(&local, &utc);
    return utc;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                  static_cast<int>(wide.size()),
                                  nullptr, 0, nullptr, nullptr);
    std::string result(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                        static_cast<int>(wide.size()),
                        result.data(), sz, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                  static_cast<int>(utf8.size()),
                                  nullptr, 0);
    std::wstring result(sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                        static_cast<int>(utf8.size()),
                        result.data(), sz);
    return result;
}

std::optional<FileInfo> GetFileInfo(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr))
        return std::nullopt;

    FileInfo fi;
    fi.full_path   = path;
    fi.is_directory = (attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    size_t sep = path.rfind(L'\\');
    if (sep == std::wstring::npos) sep = path.rfind(L'/');
    fi.name = (sep != std::wstring::npos) ? path.substr(sep + 1) : path;

    size_t dot = fi.name.rfind(L'.');
    if (dot != std::wstring::npos) {
        fi.extension = fi.name.substr(dot);
        fi.stem      = fi.name.substr(0, dot);
        for (auto& c : fi.extension) c = towlower(c);
    } else {
        fi.stem = fi.name;
    }

    LARGE_INTEGER li;
    li.HighPart = static_cast<LONG>(attr.nFileSizeHigh);
    li.LowPart  = attr.nFileSizeLow;
    fi.size_bytes    = static_cast<uint64_t>(li.QuadPart);
    fi.date_created  = FileTimeToTm(attr.ftCreationTime);
    fi.date_modified = FileTimeToTm(attr.ftLastWriteTime);

    return fi;
}

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) &&
           (attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::vector<FileInfo> ScanDirectory(const std::wstring& dir, bool recursive, bool photos_only) {
    std::vector<FileInfo> results;
    std::wstring pattern = dir;
    if (pattern.back() != L'\\') pattern += L'\\';
    pattern += L"*";

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return results;

    std::wstring base = dir;
    if (base.back() != L'\\') base += L'\\';

    do {
        std::wstring name = ffd.cFileName;
        if (name == L"." || name == L"..") continue;

        std::wstring full = base + name;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive) {
                auto sub = ScanDirectory(full, true, photos_only);
                results.insert(results.end(), sub.begin(), sub.end());
            }
        } else {
            if (!photos_only || IsPhotoFile(name)) {
                auto fi = GetFileInfo(full);
                if (fi) results.push_back(*fi);
            }
        }
    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);
    return results;
}

bool CreateDirectoryPath(const std::wstring& path) {
    if (DirectoryExists(path)) return true;

    size_t pos = 0;
    while (pos < path.size()) {
        size_t next = path.find_first_of(L"\\/", pos + 1);
        if (next == std::wstring::npos) next = path.size();
        std::wstring part = path.substr(0, next);
        if (!part.empty() && !DirectoryExists(part)) {
            if (!CreateDirectoryW(part.c_str(), nullptr)) {
                if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
            }
        }
        pos = next;
    }
    return true;
}

struct CopyContext {
    ProgressCallback cb;
    uint64_t total_bytes;
    std::wstring current_file;
};

static DWORD CALLBACK CopyProgressRoutine(
    LARGE_INTEGER TotalFileSize, LARGE_INTEGER TotalBytesTransferred,
    LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE,
    LPVOID lpData)
{
    if (!lpData) return PROGRESS_CONTINUE;
    auto* ctx = static_cast<CopyContext*>(lpData);
    if (ctx->cb) {
        CopyProgress p;
        p.bytes_transferred = static_cast<uint64_t>(TotalBytesTransferred.QuadPart);
        p.total_bytes       = static_cast<uint64_t>(TotalFileSize.QuadPart);
        p.current_file      = ctx->current_file;
        ctx->cb(p);
    }
    return PROGRESS_CONTINUE;
}

bool CopyPhoto(const std::wstring& src, const std::wstring& dst,
               bool overwrite, ProgressCallback cb)
{
    std::wstring dest = dst;

    if (DirectoryExists(dst)) {
        size_t sep = src.rfind(L'\\');
        std::wstring fname = (sep != std::wstring::npos) ? src.substr(sep + 1) : src;
        dest = dst;
        if (dest.back() != L'\\') dest += L'\\';
        dest += fname;
    }

    if (!overwrite && FileExists(dest)) dest = MakeUniquePath(dest);

    CopyContext ctx{cb, 0, src};
    BOOL cancel = FALSE;

    if (cb) {
        return CopyFileExW(src.c_str(), dest.c_str(),
                           CopyProgressRoutine, &ctx,
                           &cancel, overwrite ? 0 : COPY_FILE_FAIL_IF_EXISTS) != 0;
    }
    return CopyFileW(src.c_str(), dest.c_str(), overwrite ? FALSE : TRUE) != 0;
}

bool MovePhoto(const std::wstring& src, const std::wstring& dst) {
    std::wstring dest = dst;
    if (DirectoryExists(dst)) {
        size_t sep = src.rfind(L'\\');
        std::wstring fname = (sep != std::wstring::npos) ? src.substr(sep + 1) : src;
        dest = dst;
        if (dest.back() != L'\\') dest += L'\\';
        dest += fname;
    }
    return MoveFileExW(src.c_str(), dest.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
}

bool DeletePhoto(const std::wstring& path, bool to_recycle_bin) {
    if (!to_recycle_bin) return DeleteFileW(path.c_str()) != 0;

    std::wstring double_null = path + L'\0';
    SHFILEOPSTRUCTW op{};
    op.wFunc  = FO_DELETE;
    op.pFrom  = double_null.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    return SHFileOperationW(&op) == 0;
}

std::vector<DriveInfo> EnumerateDrives() {
    std::vector<DriveInfo> drives;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1 << i))) continue;
        std::wstring letter;
        letter += static_cast<wchar_t>(L'A' + i);
        letter += L":\\";

        DriveInfo di;
        di.letter     = letter;
        di.drive_type = GetDriveTypeW(letter.c_str());

        wchar_t label[MAX_PATH], fsname[MAX_PATH];
        if (GetVolumeInformationW(letter.c_str(), label, MAX_PATH,
                                   nullptr, nullptr, nullptr, fsname, MAX_PATH)) {
            di.label      = label;
            di.filesystem = fsname;
        }

        ULARGE_INTEGER free, total;
        if (GetDiskFreeSpaceExW(letter.c_str(), nullptr, &total, &free)) {
            di.total_bytes = total.QuadPart;
            di.free_bytes  = free.QuadPart;
        }
        drives.push_back(di);
    }
    return drives;
}

bool OpenWithDefaultApp(const std::wstring& path) {
    HINSTANCE result = ShellExecuteW(nullptr, L"open",
                                      path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
}

bool RevealInExplorer(const std::wstring& path) {
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(path.c_str());
    if (!pidl) return false;
    HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ILFree(pidl);
    return SUCCEEDED(hr);
}

std::wstring GetTempDirectory() {
    wchar_t buf[MAX_PATH];
    GetTempPathW(MAX_PATH, buf);
    return buf;
}

std::wstring SanitizeFileName(const std::wstring& name) {
    static const std::wstring illegal = L"\\/:*?\"<>|";
    std::wstring result = name;
    for (auto& c : result) {
        if (illegal.find(c) != std::wstring::npos) c = L'_';
    }
    while (!result.empty() && (result.back() == L'.' || result.back() == L' '))
        result.pop_back();
    return result;
}

std::wstring MakeUniquePath(const std::wstring& desired) {
    if (!FileExists(desired)) return desired;
    size_t dot = desired.rfind(L'.');
    std::wstring stem = (dot != std::wstring::npos) ? desired.substr(0, dot) : desired;
    std::wstring ext  = (dot != std::wstring::npos) ? desired.substr(dot) : L"";
    for (int n = 1; n < 10000; ++n) {
        std::wstring candidate = stem + L"_" + std::to_wstring(n) + ext;
        if (!FileExists(candidate)) return candidate;
    }
    return desired;
}

} // namespace PhotoManager::FileSystem