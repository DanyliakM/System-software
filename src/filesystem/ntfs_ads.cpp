#include "ntfs_ads.hpp"
#include "file_ops.hpp"
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>

namespace PhotoManager::NTFS {

static std::wstring streamPath(const std::wstring& file, const std::wstring& stream) {
    return file + L":" + stream;
}

bool AlternateDataStreams::write(const std::wstring& file_path,
                                  const std::wstring& stream_name,
                                  const std::string&  data)
{
    std::wstring sp = streamPath(file_path, stream_name);
    HANDLE h = CreateFileW(sp.c_str(), GENERIC_WRITE,
                            FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD written;
    bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()),
                        &written, nullptr) && written == data.size();
    CloseHandle(h);
    return ok;
}

std::optional<std::string> AlternateDataStreams::read(const std::wstring& file_path,
                                                        const std::wstring& stream_name)
{
    std::wstring sp = streamPath(file_path, stream_name);
    HANDLE h = CreateFileW(sp.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart == 0) {
        CloseHandle(h);
        return std::string{};
    }

    std::string result(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD readBytes;
    bool ok = ReadFile(h, result.data(), static_cast<DWORD>(sz.QuadPart),
                       &readBytes, nullptr) != 0;
    CloseHandle(h);
    if (!ok) return std::nullopt;
    result.resize(readBytes);
    return result;
}

bool AlternateDataStreams::remove(const std::wstring& file_path,
                                   const std::wstring& stream_name)
{
    std::wstring sp = streamPath(file_path, stream_name);
    return DeleteFileW(sp.c_str()) != 0;
}

bool AlternateDataStreams::exists(const std::wstring& file_path,
                                   const std::wstring& stream_name)
{
    std::wstring sp = streamPath(file_path, stream_name);
    HANDLE h = CreateFileW(sp.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

std::vector<AdsEntry> AlternateDataStreams::listStreams(const std::wstring& file_path)
{
    std::vector<AdsEntry> result;

    WIN32_FIND_STREAM_DATA fsd;
    HANDLE h = FindFirstStreamW(file_path.c_str(),
                                 FindStreamInfoStandard, &fsd, 0);
    if (h == INVALID_HANDLE_VALUE) return result;

    do {
        std::wstring name = fsd.cStreamName;
        if (name == L"::$DATA") continue;
        size_t colon2 = name.rfind(L':');
        if (colon2 != std::wstring::npos) name = name.substr(1, colon2 - 1);

        AdsEntry e;
        e.stream_name = name;
        e.size        = static_cast<uint64_t>(fsd.StreamSize.QuadPart);

        auto data = read(file_path, name);
        if (data) e.data = *data;
        result.push_back(std::move(e));

    } while (FindNextStreamW(h, &fsd));

    FindClose(h);
    return result;
}

bool AlternateDataStreams::writeHash(const std::wstring& file_path,
                                      const std::string&  hex_hash,
                                      const std::string&  algorithm)
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream ss;
    ss << algorithm << "\n" << hex_hash << "\n"
       << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return write(file_path, STREAM_HASH, ss.str());
}

std::optional<std::string> AlternateDataStreams::readHash(const std::wstring& file_path,
                                                           std::string* algorithm)
{
    auto data = read(file_path, STREAM_HASH);
    if (!data) return std::nullopt;

    std::istringstream ss(*data);
    std::string algo, hash;
    std::getline(ss, algo);
    std::getline(ss, hash);
    if (algorithm) *algorithm = algo;
    return hash;
}

bool AlternateDataStreams::writeTags(const std::wstring& file_path,
                                      const std::vector<std::string>& tags)
{
    std::string joined;
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i) joined += '\n';
        joined += tags[i];
    }
    return write(file_path, STREAM_TAGS, joined);
}

std::vector<std::string> AlternateDataStreams::readTags(const std::wstring& file_path)
{
    auto data = read(file_path, STREAM_TAGS);
    if (!data || data->empty()) return {};
    std::vector<std::string> tags;
    std::istringstream ss(*data);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) tags.push_back(line);
    }
    return tags;
}

bool AlternateDataStreams::writeRating(const std::wstring& file_path, int rating)
{
    return write(file_path, STREAM_RATING, std::to_string(rating));
}

int AlternateDataStreams::readRating(const std::wstring& file_path)
{
    auto data = read(file_path, STREAM_RATING);
    if (!data || data->empty()) return 0;
    try { return std::stoi(*data); } catch (...) { return 0; }
}

bool AlternateDataStreams::writeNote(const std::wstring& file_path,
                                      const std::string& note)
{
    return write(file_path, STREAM_NOTE, note);
}

std::optional<std::string> AlternateDataStreams::readNote(const std::wstring& file_path)
{
    return read(file_path, STREAM_NOTE);
}

bool AlternateDataStreams::markVerified(const std::wstring& file_path,
                                         const std::string& timestamp)
{
    return write(file_path, STREAM_VERIFIED, "verified:" + timestamp);
}

bool AlternateDataStreams::isVerified(const std::wstring& file_path)
{
    return exists(file_path, STREAM_VERIFIED);
}

bool AlternateDataStreams::copyStreams(const std::wstring& src,
                                        const std::wstring& dst)
{
    auto streams = listStreams(src);
    bool all_ok = true;
    for (auto& s : streams) {
        if (!write(dst, s.stream_name, s.data)) all_ok = false;
    }
    return all_ok;
}

bool AlternateDataStreams::stripAllStreams(const std::wstring& file_path)
{
    auto streams = listStreams(file_path);
    bool all_ok = true;
    for (auto& s : streams) {
        if (!remove(file_path, s.stream_name)) all_ok = false;
    }
    return all_ok;
}

bool AlternateDataStreams::isNtfsVolume(const std::wstring& file_path)
{
    wchar_t root[MAX_PATH];
    if (!GetVolumePathNameW(file_path.c_str(), root, MAX_PATH)) return false;
    wchar_t fsname[32];
    if (!GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr,
                                nullptr, fsname, 32)) return false;
    return std::wstring(fsname) == L"NTFS";
}

std::unordered_map<std::string, std::string>
AlternateDataStreams::readAllMetadata(const std::wstring& file_path)
{
    std::unordered_map<std::string, std::string> meta;
    auto streams = listStreams(file_path);
    for (auto& s : streams) {
        std::string key = FileSystem::WideToUtf8(s.stream_name);
        meta[key] = s.data;
    }
    return meta;
}

} // namespace PhotoManager::NTFS