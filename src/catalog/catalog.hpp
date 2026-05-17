#pragma once
#include "../exif/exif_parser.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <cstdint>

namespace PhotoManager::Catalog {

struct PhotoRecord {
    uint64_t    id;
    std::string path;
    std::string hash;
    uint64_t    file_size;

    std::string date_taken;
    std::string date_imported;
    std::string date_modified;

    std::string camera_make;
    std::string camera_model;
    std::string lens_model;

    uint32_t    image_width;
    uint32_t    image_height;
    double      focal_length;
    double      f_number;
    double      exposure_time;
    int32_t     iso;

    bool        has_gps;
    double      gps_lat;
    double      gps_lon;

    std::string keywords;
    std::string tags;
    std::string description;
    std::string artist;
    std::string copyright;

    std::string collection;
    int         rating;
    bool        flagged;

    bool populated() const { return !path.empty(); }
};

struct SearchFilter {
    std::string camera_make;
    std::string camera_model;
    std::string lens_model;
    std::string date_from;
    std::string date_to;
    std::string keyword;
    std::string tag;
    std::string collection;
    int         min_rating   = -1;
    bool        flagged_only = false;
    uint32_t    min_width    = 0;
    uint32_t    min_height   = 0;
};

class PhotoCatalog {
public:
    explicit PhotoCatalog(const std::string& db_path);

    bool load();
    bool save() const;

    uint64_t addPhoto(const PhotoRecord& rec);
    bool     updatePhoto(const PhotoRecord& rec);
    bool     removePhoto(uint64_t id);
    bool     removeByPath(const std::string& path);

    std::optional<PhotoRecord> findById(uint64_t id) const;
    std::optional<PhotoRecord> findByPath(const std::string& path) const;
    bool                        hasPath(const std::string& path) const;

    std::vector<PhotoRecord> search(const SearchFilter& filter) const;
    std::vector<PhotoRecord> getAll() const;
    std::vector<PhotoRecord> getByCollection(const std::string& collection) const;
    std::vector<PhotoRecord> getByDateRange(const std::string& from,
                                             const std::string& to) const;
    std::vector<PhotoRecord> getDuplicates() const;

    std::vector<std::string> getDistinctCameras() const;
    std::vector<std::string> getDistinctLenses() const;
    std::vector<std::string> getDistinctKeywords() const;
    std::vector<std::string> getDistinctCollections() const;

    void setRating(uint64_t id, int rating);
    void setFlagged(uint64_t id, bool flagged);
    void addKeyword(uint64_t id, const std::string& keyword);
    void addTag(uint64_t id, const std::string& tag);
    void setCollection(uint64_t id, const std::string& collection);

    size_t count() const;
    void   clear();

    static PhotoRecord fromExif(const std::string& path, uint64_t size,
                                 const std::string& hash,
                                 const Exif::ExifData& exif);

private:
    std::string serialize(const PhotoRecord& r) const;
    bool        deserialize(const std::string& line, PhotoRecord& r) const;

    std::string                               db_path_;
    std::unordered_map<uint64_t, PhotoRecord> records_;
    std::unordered_map<std::string, uint64_t> path_index_;
    uint64_t                                  next_id_;

    static std::string escape(const std::string& s);
    static std::string unescape(const std::string& s);
    static std::string currentDateStr();
};

} // namespace PhotoManager::Catalog