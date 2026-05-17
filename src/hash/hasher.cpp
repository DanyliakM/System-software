#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "hasher.hpp"
#include "../filesystem/file_ops.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace PhotoManager::Hash {

namespace {
constexpr size_t READ_BUFFER = 1024 * 1024;
}

Hasher::Hasher(const std::string& cache_file_path)
    : cache_path_(cache_file_path)
{
    if (!cache_path_.empty()) loadCache();
}

int64_t Hasher::getFileModTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr))
        return -1;
    LARGE_INTEGER li;
    li.HighPart = static_cast<LONG>(attr.ftLastWriteTime.dwHighDateTime);
    li.LowPart  = attr.ftLastWriteTime.dwLowDateTime;
    return static_cast<int64_t>(li.QuadPart);
}

uint64_t Hasher::getFileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attr))
        return 0;
    LARGE_INTEGER li;
    li.HighPart = static_cast<LONG>(attr.nFileSizeHigh);
    li.LowPart  = attr.nFileSizeLow;
    return static_cast<uint64_t>(li.QuadPart);
}

std::string Hasher::computeFileHash(const std::wstring& path) {
    std::string utf8 = FileSystem::WideToUtf8(path);
    uint64_t sz      = getFileSize(path);
    int64_t  mt      = getFileModTime(path);

    if (isCacheValid(utf8, sz, mt)) {
        return cache_.at(utf8).hex_hash;
    }

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open file: " + utf8);

    SHA256 hasher;
    std::vector<uint8_t> buf(READ_BUFFER);
    DWORD read;

    while (ReadFile(hFile, buf.data(), static_cast<DWORD>(READ_BUFFER), &read, nullptr)
           && read > 0)
    {
        hasher.update(buf.data(), static_cast<size_t>(read));
    }
    CloseHandle(hFile);

    std::string hex = SHA256::toHexString(hasher.finalize());
    storeHash(utf8, hex, sz, mt);
    if (!cache_path_.empty()) saveCache();
    return hex;
}

bool Hasher::verifyIntegrity(const std::wstring& path, const std::string& expected_hex) {
    try {
        std::string actual = computeFileHash(path);
        return actual == expected_hex;
    } catch (...) {
        return false;
    }
}

bool Hasher::filesAreIdentical(const std::wstring& a, const std::wstring& b) {
    if (getFileSize(a) != getFileSize(b)) return false;
    return computeFileHash(a) == computeFileHash(b);
}

std::optional<HashRecord> Hasher::getCached(const std::string& utf8_path) const {
    auto it = cache_.find(utf8_path);
    if (it == cache_.end()) return std::nullopt;
    return it->second;
}

void Hasher::storeHash(const std::string& utf8_path, const std::string& hex,
                        uint64_t file_size, int64_t mtime)
{
    HashRecord r;
    r.file_path = utf8_path;
    r.hex_hash  = hex;
    r.file_size = file_size;
    r.file_mtime= mtime;
    r.verified  = true;
    cache_[utf8_path] = r;
}

bool Hasher::isCacheValid(const std::string& utf8_path,
                           uint64_t cur_size, int64_t cur_mtime) const
{
    auto it = cache_.find(utf8_path);
    if (it == cache_.end()) return false;
    return it->second.file_size == cur_size &&
           it->second.file_mtime == cur_mtime;
}

void Hasher::loadCache() {
    std::ifstream in(cache_path_);
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        HashRecord r;
        std::string sz, mt;
        if (!std::getline(ss, r.hex_hash, '|')) continue;
        if (!std::getline(ss, sz, '|')) continue;
        if (!std::getline(ss, mt, '|')) continue;
        if (!std::getline(ss, r.file_path)) continue;
        try {
            r.file_size  = std::stoull(sz);
            r.file_mtime = std::stoll(mt);
            r.verified   = true;
            cache_[r.file_path] = r;
        } catch (...) {}
    }
}

void Hasher::saveCache() const {
    if (cache_path_.empty()) return;
    std::ofstream out(cache_path_, std::ios::trunc);
    if (!out.is_open()) return;
    out << "# PhotoManager Hash Cache v1\n";
    for (auto& [path, rec] : cache_) {
        out << rec.hex_hash << '|'
            << rec.file_size << '|'
            << rec.file_mtime << '|'
            << rec.file_path << '\n';
    }
}

void Hasher::clearCache() { cache_.clear(); }
size_t Hasher::cacheSize() const { return cache_.size(); }

} // namespace PhotoManager::Hash