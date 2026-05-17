#pragma once
#include "sha256.hpp"
#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace PhotoManager::Hash {

struct HashRecord {
    std::string  file_path;
    std::string  hex_hash;
    uint64_t     file_size;
    int64_t      file_mtime;
    bool         verified;
};

class Hasher {
public:
    explicit Hasher(const std::string& cache_file_path = "");

    std::string              computeFileHash(const std::wstring& path);
    bool                     verifyIntegrity(const std::wstring& path,
                                             const std::string& expected_hex);
    bool                     filesAreIdentical(const std::wstring& a,
                                               const std::wstring& b);

    std::optional<HashRecord> getCached(const std::string& utf8_path) const;
    void                     storeHash(const std::string& utf8_path,
                                       const std::string& hex,
                                       uint64_t file_size,
                                       int64_t  mtime);

    void loadCache();
    void saveCache() const;
    void clearCache();
    size_t cacheSize() const;

    static int64_t getFileModTime(const std::wstring& path);
    static uint64_t getFileSize(const std::wstring& path);

private:
    bool isCacheValid(const std::string& utf8_path,
                      uint64_t cur_size, int64_t cur_mtime) const;

    std::string                               cache_path_;
    std::unordered_map<std::string, HashRecord> cache_;
};

} // namespace PhotoManager::Hash