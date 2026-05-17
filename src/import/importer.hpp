#pragma once
#include "../filesystem/file_ops.hpp"
#include "../catalog/catalog.hpp"
#include "../hash/hasher.hpp"
#include "../exif/exif_parser.hpp"
#include <string>
#include <vector>
#include <functional>

namespace PhotoManager::Import {

enum class OrganizeMode {
    BY_DATE_FLAT,
    BY_YEAR_MONTH,
    BY_YEAR_MONTH_DAY,
    BY_CAMERA,
    BY_CAMERA_DATE,
    FLAT
};

struct ImportOptions {
    std::string    source_path;
    std::string    destination_path;
    bool           recursive           = true;
    bool           delete_source       = false;
    bool           skip_duplicates     = true;
    bool           compute_hash        = true;
    OrganizeMode   organize_mode       = OrganizeMode::BY_YEAR_MONTH;
    std::string    collection_name;
    std::string    event_name;
};

struct ImportProgress {
    int     total;
    int     processed;
    int     imported;
    int     skipped;
    int     errors;
    std::string current_file;
};

struct ImportResult {
    int     total_found;
    int     imported;
    int     duplicates_skipped;
    int     errors;
    std::vector<std::string> imported_paths;
    std::vector<std::pair<std::string,std::string>> error_list;
};

using ProgressCallback = std::function<void(const ImportProgress&)>;

class Importer {
public:
    Importer(Catalog::PhotoCatalog& catalog, Hash::Hasher& hasher);

    ImportResult importFromPath(const ImportOptions& options,
                                 ProgressCallback cb = nullptr);

    ImportResult importFromDrive(const std::wstring& drive_letter,
                                  const ImportOptions& options,
                                  ProgressCallback cb = nullptr);

    std::string buildDestinationPath(const std::string& dest_root,
                                      const Exif::ExifData& exif,
                                      const FileSystem::FileInfo& fi,
                                      OrganizeMode mode,
                                      const std::string& event) const;

    std::vector<FileSystem::DriveInfo> getRemovableDrives() const;

private:
    ImportResult processFiles(const std::vector<FileSystem::FileInfo>& files,
                               const ImportOptions& opts,
                               ProgressCallback cb);

    Catalog::PhotoCatalog& catalog_;
    Hash::Hasher&          hasher_;
};

} // namespace PhotoManager::Import