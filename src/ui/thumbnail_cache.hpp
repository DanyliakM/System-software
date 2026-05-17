#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <mutex>

namespace PhotoManager::Thumb {

struct ThumbnailInfo {
    std::wstring source_path;
    std::wstring thumb_path;
    uint32_t     thumb_width;
    uint32_t     thumb_height;
    uint64_t     source_size;
    int64_t      source_mtime;
};

class ThumbnailCache {
public:
    explicit ThumbnailCache(const std::wstring& cache_dir,
                             uint32_t thumb_size = 256);
    ~ThumbnailCache() = default;

    std::optional<std::wstring> getThumbnail(const std::wstring& source_path);
    bool                        generateThumbnail(const std::wstring& source_path,
                                                   std::wstring& out_thumb_path);
    bool                        hasCached(const std::wstring& source_path) const;
    void                        invalidate(const std::wstring& source_path);
    void                        clearCache();

    size_t                      cacheCount() const;
    uint64_t                    cacheSizeBytes() const;

    std::vector<ThumbnailInfo>  listCached() const;

    bool                        openThumbnailAsConsolePreview(
                                    const std::wstring& source_path) const;

    bool                        generateAsciiPreview(const std::wstring& source_path,
                                                      uint32_t cols = 60,
                                                      uint32_t rows = 24) const;

    bool                        extractWindowsThumbnail(const std::wstring& source_path,
                                                         std::wstring& out_bmp_path);

    void                        setThumbSize(uint32_t size);
    uint32_t                    thumbSize() const;

private:
    std::wstring makeCachePath(const std::wstring& source_path) const;
    bool         isCacheValid(const std::wstring& thumb_path,
                               const std::wstring& source_path) const;

    bool loadJpegViaBitmap(const std::wstring& source_path,
                            HBITMAP& out_bitmap,
                            uint32_t& out_w, uint32_t& out_h) const;
    bool saveBitmap(HBITMAP hbmp, HDC hdc,
                     uint32_t w, uint32_t h,
                     const std::wstring& path) const;

    std::wstring             cache_dir_;
    uint32_t                 thumb_size_;
    mutable std::mutex       mutex_;
    std::unordered_map<std::wstring, ThumbnailInfo> index_;
};

} // namespace PhotoManager::Thumb