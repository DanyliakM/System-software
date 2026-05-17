#include "../filesystem/file_ops.hpp"
#include "src/hash/hasher.hpp"
#include <filesystem>
#include <shellapi.h>

#include <sstream>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>

#include <initguid.h>
#include <thumbcache.h>
#include <shlobj.h>
#include "thumbnail_cache.hpp"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

namespace PhotoManager::Thumb {

namespace {

std::wstring hashPath(const std::wstring& path) {
    uint64_t h = 14695981039346656037ULL;
    for (auto& c : path) {
        h ^= static_cast<uint64_t>(c);
        h *= 1099511628211ULL;
    }
    std::wostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill(L'0') << h;
    return ss.str();
}

} // anonymous namespace

ThumbnailCache::ThumbnailCache(const std::wstring& cache_dir, uint32_t thumb_size)
    : cache_dir_(cache_dir), thumb_size_(thumb_size)
{
    FileSystem::CreateDirectoryPath(cache_dir_);
}

std::wstring ThumbnailCache::makeCachePath(const std::wstring& source_path) const {
    return cache_dir_ + L"\\" + hashPath(source_path) + L".bmp";
}

bool ThumbnailCache::isCacheValid(const std::wstring& thumb_path,
                                   const std::wstring& source_path) const
{
    if (!FileSystem::FileExists(thumb_path)) return false;
    int64_t src_mt  = Hash::Hasher::getFileModTime(source_path);
    int64_t thm_mt  = Hash::Hasher::getFileModTime(thumb_path);
    return thm_mt >= src_mt && src_mt > 0;
}

bool ThumbnailCache::hasCached(const std::wstring& source_path) const {
    return isCacheValid(makeCachePath(source_path), source_path);
}

void ThumbnailCache::invalidate(const std::wstring& source_path) {
    std::lock_guard<std::mutex> lk(mutex_);
    std::wstring tp = makeCachePath(source_path);
    DeleteFileW(tp.c_str());
    index_.erase(source_path);
}

void ThumbnailCache::clearCache() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::wstring pattern = cache_dir_ + L"\\*.bmp";
    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &ffd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring fp = cache_dir_ + L"\\" + ffd.cFileName;
            DeleteFileW(fp.c_str());
        } while (FindNextFileW(h, &ffd));
        FindClose(h);
    }
    index_.clear();
}

bool ThumbnailCache::saveBitmap(HBITMAP hbmp, HDC hdc,
                                  uint32_t w, uint32_t h,
                                  const std::wstring& path) const
{
    BITMAPFILEHEADER bfh{};
    BITMAPINFOHEADER bih{};
    bih.biSize        = sizeof(BITMAPINFOHEADER);
    bih.biWidth       = static_cast<LONG>(w);
    bih.biHeight      = -static_cast<LONG>(h);
    bih.biPlanes      = 1;
    bih.biBitCount    = 24;
    bih.biCompression = BI_RGB;
    DWORD row_size    = ((w * 3 + 3) & ~3u);
    bih.biSizeImage   = row_size * h;

    bfh.bfType    = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize    = bfh.bfOffBits + bih.biSizeImage;

    std::vector<uint8_t> pixels(bih.biSizeImage);
    BITMAPINFO bi{};
    bi.bmiHeader = bih;
    if (!GetDIBits(hdc, hbmp, 0, h, pixels.data(), &bi, DIB_RGB_COLORS))
        return false;

    std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc);    
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(&bfh), sizeof(bfh));
    f.write(reinterpret_cast<const char*>(&bih), sizeof(bih));
    f.write(reinterpret_cast<const char*>(pixels.data()),
            static_cast<std::streamsize>(pixels.size()));
    return f.good();
}

bool ThumbnailCache::extractWindowsThumbnail(const std::wstring& source_path,
                                               std::wstring& out_bmp_path)
{
    std::wstring thumb_path = makeCachePath(source_path);
    if (isCacheValid(thumb_path, source_path)) {
        out_bmp_path = thumb_path;
        return true;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IThumbnailCache* cache = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_LocalThumbnailCache, nullptr,
                                   CLSCTX_INPROC, IID_PPV_ARGS(&cache));
    if (FAILED(hr)) {
        CoUninitialize();
        return false;
    }

    IShellItem* item = nullptr;
    hr = SHCreateItemFromParsingName(source_path.c_str(), nullptr,
                                      IID_PPV_ARGS(&item));
    if (FAILED(hr)) { cache->Release(); CoUninitialize(); return false; }

    ISharedBitmap* shared_bmp = nullptr;
    WTS_CACHEFLAGS flags;
    WTS_THUMBNAILID tid;
    hr = cache->GetThumbnail(item,
                              static_cast<UINT>(thumb_size_),
                              WTS_EXTRACT, &shared_bmp, &flags, &tid);

    bool ok = false;
    if (SUCCEEDED(hr) && shared_bmp) {
        HBITMAP hbmp = nullptr;
        if (SUCCEEDED(shared_bmp->GetSharedBitmap(&hbmp)) && hbmp) {
            BITMAP bm{};
            GetObjectW(hbmp, sizeof(bm), &bm);
            HDC hdc = CreateCompatibleDC(nullptr);
            HGDIOBJ old = SelectObject(hdc, hbmp);
            ok = saveBitmap(hbmp, hdc,
                             static_cast<uint32_t>(bm.bmWidth),
                             static_cast<uint32_t>(abs(bm.bmHeight)),
                             thumb_path);
            SelectObject(hdc, old);
            DeleteDC(hdc);
        }
        shared_bmp->Release();
    }

    item->Release();
    cache->Release();
    CoUninitialize();

    if (ok) {
        out_bmp_path = thumb_path;
        std::lock_guard<std::mutex> lk(mutex_);
        ThumbnailInfo ti;
        ti.source_path = source_path;
        ti.thumb_path  = thumb_path;
        ti.thumb_width = ti.thumb_height = thumb_size_;
        index_[source_path] = ti;
    }
    return ok;
}

std::optional<std::wstring> ThumbnailCache::getThumbnail(const std::wstring& source_path) {
    std::wstring out;
    if (extractWindowsThumbnail(source_path, out)) return out;
    return std::nullopt;
}

bool ThumbnailCache::generateThumbnail(const std::wstring& source_path,
                                         std::wstring& out_thumb_path)
{
    return extractWindowsThumbnail(source_path, out_thumb_path);
}

bool ThumbnailCache::openThumbnailAsConsolePreview(const std::wstring& source_path) const {
    std::wstring thumb_path;
    HRESULT hr = S_OK;
    (void)hr;

    if (FileSystem::FileExists(makeCachePath(source_path)))
        thumb_path = makeCachePath(source_path);
    else
        thumb_path = source_path;

    HINSTANCE res = ShellExecuteW(nullptr, L"open",
                                   thumb_path.c_str(), nullptr, nullptr, SW_SHOW);
    return reinterpret_cast<intptr_t>(res) > 32;
}

bool ThumbnailCache::generateAsciiPreview(const std::wstring& source_path,
                                            uint32_t cols, uint32_t rows) const
{
    static const char* DENSE =
        " .'`^\",:;Il!i><~+_-?][}{1)(|\\/"
        "tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";

    std::ifstream f(std::filesystem::path(source_path), std::ios::binary);
    if (!f.is_open()) return false;

    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    if (raw.size() < 10) return false;

    bool is_jpeg = raw[0]==0xFF && raw[1]==0xD8;
    if (!is_jpeg) {
        std::cout << "\n  [ASCII preview only supports JPEG in this build]\n"
                  << "  File: " << FileSystem::WideToUtf8(source_path) << '\n';
        return false;
    }

    uint32_t img_w = 0, img_h = 0;
    for (size_t i = 2; i + 8 < raw.size(); ) {
        if (raw[i] != 0xFF) break;
        uint8_t marker = raw[i+1];
        uint16_t seg_len = static_cast<uint16_t>((raw[i+2]<<8)|raw[i+3]);
        if (marker == 0xC0 || marker == 0xC2) {
            img_h = static_cast<uint32_t>((raw[i+5]<<8)|raw[i+6]);
            img_w = static_cast<uint32_t>((raw[i+7]<<8)|raw[i+8]);
            break;
        }
        if (marker == 0xDA) break;
        i += 2 + seg_len;
    }

    std::cout << "  └" << std::string(cols, '-') << "┘\n";
    if (img_w && img_h) {
        std::cout << "  │ " << FileSystem::WideToUtf8(source_path) << '\n';
        std::cout << "  │ Resolution: " << img_w << " x " << img_h << " px\n";

        double aspect = static_cast<double>(img_h) / img_w;
        uint32_t art_rows = static_cast<uint32_t>(cols * aspect * 0.45);
        art_rows = std::max(4u, std::min(art_rows, rows));

        std::cout << "  │\n";
        for (uint32_t r = 0; r < art_rows; ++r) {
            double y_frac = static_cast<double>(r) / art_rows;
            std::cout << "  │";
            for (uint32_t c = 0; c < cols; ++c) {
                double x_frac = static_cast<double>(c) / cols;
                size_t raw_idx = static_cast<size_t>(
                    (y_frac * img_h * img_w + x_frac * img_w) * 3);
                if (raw_idx + 2 < raw.size()) {
                    uint8_t lum = raw[raw_idx];
                    size_t ci = static_cast<size_t>(lum) * (strlen(DENSE)-1) / 255;
                    std::cout << DENSE[ci];
                } else {
                    std::cout << ' ';
                }
            }
            std::cout << "│\n";
        }
    } else {
        std::cout << "  │  [Could not decode image dimensions]\n";
    }
    std::cout << "  └" << std::string(cols, '-') << "┘\n";
    return true;
}

size_t ThumbnailCache::cacheCount() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return index_.size();
}

uint64_t ThumbnailCache::cacheSizeBytes() const {
    std::lock_guard<std::mutex> lk(mutex_);
    uint64_t total = 0;
    for (auto& [k,v] : index_) total += Hash::Hasher::getFileSize(v.thumb_path);
    return total;
}

std::vector<ThumbnailInfo> ThumbnailCache::listCached() const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<ThumbnailInfo> result;
    for (auto& [k,v] : index_) result.push_back(v);
    return result;
}

void ThumbnailCache::setThumbSize(uint32_t size) { thumb_size_ = size; }
uint32_t ThumbnailCache::thumbSize() const { return thumb_size_; }

} // namespace PhotoManager::Thumb