#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <cstdint>

namespace PhotoManager::NTFS {

struct AdsEntry {
    std::wstring stream_name;
    std::string  data;
    uint64_t     size;
};

class AlternateDataStreams {
public:
    static constexpr const wchar_t* STREAM_HASH      = L"PhotoManager.Hash";
    static constexpr const wchar_t* STREAM_TAGS       = L"PhotoManager.Tags";
    static constexpr const wchar_t* STREAM_RATING     = L"PhotoManager.Rating";
    static constexpr const wchar_t* STREAM_NOTE       = L"PhotoManager.Note";
    static constexpr const wchar_t* STREAM_COLLECTION = L"PhotoManager.Collection";
    static constexpr const wchar_t* STREAM_VERIFIED   = L"PhotoManager.Verified";
    static constexpr const wchar_t* STREAM_EXIFCACHE  = L"PhotoManager.ExifCache";

    static bool         write(const std::wstring& file_path,
                               const std::wstring& stream_name,
                               const std::string&  data);

    static std::optional<std::string> read(const std::wstring& file_path,
                                            const std::wstring& stream_name);

    static bool         remove(const std::wstring& file_path,
                                const std::wstring& stream_name);

    static bool         exists(const std::wstring& file_path,
                                const std::wstring& stream_name);

    static std::vector<AdsEntry> listStreams(const std::wstring& file_path);

    static bool         writeHash(const std::wstring& file_path,
                                   const std::string&  hex_hash,
                                   const std::string&  algorithm = "SHA-256");

    static std::optional<std::string> readHash(const std::wstring& file_path,
                                                std::string* algorithm = nullptr);

    static bool         writeTags(const std::wstring& file_path,
                                   const std::vector<std::string>& tags);

    static std::vector<std::string> readTags(const std::wstring& file_path);

    static bool         writeRating(const std::wstring& file_path, int rating);
    static int          readRating(const std::wstring& file_path);

    static bool         writeNote(const std::wstring& file_path,
                                   const std::string& note);
    static std::optional<std::string> readNote(const std::wstring& file_path);

    static bool         markVerified(const std::wstring& file_path,
                                      const std::string&  timestamp);
    static bool         isVerified(const std::wstring& file_path);

    static bool         copyStreams(const std::wstring& src,
                                     const std::wstring& dst);
    static bool         stripAllStreams(const std::wstring& file_path);
    static bool         isNtfsVolume(const std::wstring& file_path);

    static std::unordered_map<std::string, std::string>
                        readAllMetadata(const std::wstring& file_path);
};

} // namespace PhotoManager::NTFS