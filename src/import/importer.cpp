#include "importer.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iostream>


using namespace std::string_literals;

namespace PhotoManager::Import {

Importer::Importer(Catalog::PhotoCatalog& catalog, Hash::Hasher& hasher)
    : catalog_(catalog), hasher_(hasher)
{}

std::vector<FileSystem::DriveInfo> Importer::getRemovableDrives() const {
    auto all = FileSystem::EnumerateDrives();
    std::vector<FileSystem::DriveInfo> removable;
    for (auto& d : all) {
        if (d.drive_type == DRIVE_REMOVABLE || d.drive_type == DRIVE_CDROM)
            removable.push_back(d);
    }
    return removable;
}

std::string Importer::buildDestinationPath(const std::string& dest_root,
                                            const Exif::ExifData& exif,
                                            const FileSystem::FileInfo& fi,
                                            OrganizeMode mode,
                                            const std::string& event) const
{
    std::string dt = exif.datetime_original;
    if (dt.empty()) dt = exif.datetime;

    auto safeGet = [&](size_t pos, size_t len) -> std::string {
        if (dt.size() > pos + len - 1) return dt.substr(pos, len);
        return "0000"s.substr(0, len);
    };

    std::string year  = safeGet(0, 4);
    std::string month = safeGet(5, 2);
    std::string day   = safeGet(8, 2);

    if (year == "0000") {
        std::ostringstream ss;
        ss << std::setw(4) << std::setfill('0') << (fi.date_created.tm_year + 1900);
        year = ss.str();
        ss.str("");
        ss << std::setw(2) << std::setfill('0') << (fi.date_created.tm_mon + 1);
        month = ss.str();
        ss.str("");
        ss << std::setw(2) << std::setfill('0') << fi.date_created.tm_mday;
        day = ss.str();
    }

    std::string sub;
    switch (mode) {
    case OrganizeMode::BY_YEAR_MONTH:
        sub = year + "\\" + year + "-" + month;
        break;
    case OrganizeMode::BY_YEAR_MONTH_DAY:
        sub = year + "\\" + year + "-" + month + "\\" + year + "-" + month + "-" + day;
        break;
    case OrganizeMode::BY_DATE_FLAT:
        sub = year + "-" + month + "-" + day;
        break;
    case OrganizeMode::BY_CAMERA: {
        std::string cam = exif.camera_model.empty() ? "Unknown" : exif.camera_model;
        for (auto& c : cam) if (c == '/' || c == '\\' || c == ':') c = '_';
        sub = cam;
        break;
    }
    case OrganizeMode::BY_CAMERA_DATE: {
        std::string cam = exif.camera_model.empty() ? "Unknown" : exif.camera_model;
        for (auto& c : cam) if (c == '/' || c == '\\' || c == ':') c = '_';
        sub = cam + "\\" + year + "-" + month;
        break;
    }
    case OrganizeMode::FLAT:
    default:
        sub = "";
        break;
    }

    if (!event.empty()) sub = event + (sub.empty() ? "" : "\\" + sub);

    std::string result = dest_root;
    if (result.back() != '\\') result += '\\';
    if (!sub.empty()) result += sub;
    return result;
}

ImportResult Importer::processFiles(const std::vector<FileSystem::FileInfo>& files,
                                     const ImportOptions& opts,
                                     ProgressCallback cb)
{
    ImportResult result{};
    result.total_found = static_cast<int>(files.size());

    ImportProgress progress{};
    progress.total = static_cast<int>(files.size());

    for (auto& fi : files) {
        ++progress.processed;
        progress.current_file = FileSystem::WideToUtf8(fi.name);
        if (cb) cb(progress);

        std::string utf8_src = FileSystem::WideToUtf8(fi.full_path);

        std::string hash;
        if (opts.compute_hash) {
            try { hash = hasher_.computeFileHash(fi.full_path); }
            catch (...) { hash = ""; }
        }

        if (opts.skip_duplicates && !hash.empty() && catalog_.hasPath(utf8_src)) {
            ++result.duplicates_skipped;
            ++progress.skipped;
            continue;
        }

        auto exif = Exif::ExifParser::parse(fi.full_path);
        Exif::ExifData exif_data;
        if (exif) exif_data = *exif;

        std::string dest_dir = buildDestinationPath(
            opts.destination_path, exif_data, fi,
            opts.organize_mode, opts.event_name);

        std::wstring dest_dir_w = FileSystem::Utf8ToWide(dest_dir);
        if (!FileSystem::CreateDirectoryPath(dest_dir_w)) {
            ++result.errors;
            ++progress.errors;
            result.error_list.emplace_back(utf8_src, "Cannot create directory: " + dest_dir);
            continue;
        }

        std::wstring dest_path_w = dest_dir_w + L"\\" + fi.name;
        dest_path_w = FileSystem::MakeUniquePath(dest_path_w);

        bool ok;
        if (opts.delete_source)
            ok = FileSystem::MovePhoto(fi.full_path, dest_path_w);
        else
            ok = FileSystem::CopyPhoto(fi.full_path, dest_path_w, false);

        if (!ok) {
            ++result.errors;
            ++progress.errors;
            result.error_list.emplace_back(utf8_src, "Copy/move failed");
            continue;
        }

        std::string utf8_dst = FileSystem::WideToUtf8(dest_path_w);

        Catalog::PhotoRecord rec = Catalog::PhotoCatalog::fromExif(
            utf8_dst, fi.size_bytes, hash, exif_data);
        rec.date_modified = FileSystem::WideToUtf8(fi.name);

        if (!opts.collection_name.empty()) rec.collection = opts.collection_name;
        if (!opts.event_name.empty())      rec.tags       = opts.event_name;

        catalog_.addPhoto(rec);

        ++result.imported;
        ++progress.imported;
        result.imported_paths.push_back(utf8_dst);
    }

    return result;
}

ImportResult Importer::importFromPath(const ImportOptions& options,
                                       ProgressCallback cb)
{
    std::wstring src_w = FileSystem::Utf8ToWide(options.source_path);
    auto files = FileSystem::ScanDirectory(src_w, options.recursive, true);
    return processFiles(files, options, cb);
}

ImportResult Importer::importFromDrive(const std::wstring& drive_letter,
                                        const ImportOptions& options,
                                        ProgressCallback cb)
{
    ImportOptions opts = options;
    opts.source_path = FileSystem::WideToUtf8(drive_letter);
    return importFromPath(opts, cb);
}

} // namespace PhotoManager::Import