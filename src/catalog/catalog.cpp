#include "catalog.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <stdexcept>
#include <set>

namespace PhotoManager::Catalog {

namespace {

std::vector<std::string> splitLine(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, delim)) parts.push_back(tok);
    return parts;
}

bool containsICase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(),
                           needle.begin(), needle.end(),
                           [](char a, char b){ return std::tolower(a)==std::tolower(b); });
    return it != haystack.end();
}

} // anonymous namespace

std::string PhotoCatalog::escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '|') out += "\\|";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string PhotoCatalog::unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i+1 < s.size()) {
            ++i;
            if (s[i] == '\\') out += '\\';
            else if (s[i] == '|') out += '|';
            else if (s[i] == 'n') out += '\n';
            else { out += '\\'; out += s[i]; }
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string PhotoCatalog::currentDateStr() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

PhotoCatalog::PhotoCatalog(const std::string& db_path)
    : db_path_(db_path), next_id_(1)
{}

std::string PhotoCatalog::serialize(const PhotoRecord& r) const {
    std::ostringstream ss;
    auto e = [](const std::string& s){ return PhotoCatalog::escape(s); };
    ss << r.id               << '|'
       << e(r.path)          << '|'
       << e(r.hash)          << '|'
       << r.file_size        << '|'
       << e(r.date_taken)    << '|'
       << e(r.date_imported) << '|'
       << e(r.date_modified) << '|'
       << e(r.camera_make)   << '|'
       << e(r.camera_model)  << '|'
       << e(r.lens_model)    << '|'
       << r.image_width      << '|'
       << r.image_height     << '|'
       << r.focal_length     << '|'
       << r.f_number         << '|'
       << r.exposure_time    << '|'
       << r.iso              << '|'
       << (r.has_gps ? 1 : 0) << '|'
       << r.gps_lat          << '|'
       << r.gps_lon          << '|'
       << e(r.keywords)      << '|'
       << e(r.tags)          << '|'
       << e(r.description)   << '|'
       << e(r.artist)        << '|'
       << e(r.copyright)     << '|'
       << e(r.collection)    << '|'
       << r.rating           << '|'
       << (r.flagged ? 1 : 0);
    return ss.str();
}

bool PhotoCatalog::deserialize(const std::string& line, PhotoRecord& r) const {
    auto u = [](const std::string& s){ return PhotoCatalog::unescape(s); };
    std::vector<std::string> parts;
    std::string cur;
    bool escaped = false;
    for (char c : line) {
        if (escaped) {
            cur += '\\';
            cur += c;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '|') {
            parts.push_back(u(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    parts.push_back(u(cur));

    if (parts.size() < 27) return false;
    try {
        size_t i = 0;
        r.id             = std::stoull(parts[i++]);
        r.path           = parts[i++];
        r.hash           = parts[i++];
        r.file_size      = std::stoull(parts[i++]);
        r.date_taken     = parts[i++];
        r.date_imported  = parts[i++];
        r.date_modified  = parts[i++];
        r.camera_make    = parts[i++];
        r.camera_model   = parts[i++];
        r.lens_model     = parts[i++];
        r.image_width    = static_cast<uint32_t>(std::stoul(parts[i++]));
        r.image_height   = static_cast<uint32_t>(std::stoul(parts[i++]));
        r.focal_length   = std::stod(parts[i++]);
        r.f_number       = std::stod(parts[i++]);
        r.exposure_time  = std::stod(parts[i++]);
        r.iso            = std::stoi(parts[i++]);
        r.has_gps        = parts[i++] != "0";
        r.gps_lat        = std::stod(parts[i++]);
        r.gps_lon        = std::stod(parts[i++]);
        r.keywords       = parts[i++];
        r.tags           = parts[i++];
        r.description    = parts[i++];
        r.artist         = parts[i++];
        r.copyright      = parts[i++];
        r.collection     = parts[i++];
        r.rating         = std::stoi(parts[i++]);
        r.flagged        = parts[i++] != "0";
    } catch (...) {
        return false;
    }
    return true;
}

bool PhotoCatalog::load() {
    std::ifstream in(db_path_);
    if (!in.is_open()) return false;
    records_.clear();
    path_index_.clear();
    next_id_ = 1;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        PhotoRecord r{};
        if (deserialize(line, r)) {
            records_[r.id] = r;
            path_index_[r.path] = r.id;
            if (r.id >= next_id_) next_id_ = r.id + 1;
        }
    }
    return true;
}

bool PhotoCatalog::save() const {
    std::ofstream out(db_path_, std::ios::trunc);
    if (!out.is_open()) return false;
    out << "# PhotoManager Catalog v1.0\n";
    out << "# id|path|hash|file_size|date_taken|date_imported|date_modified|"
           "camera_make|camera_model|lens_model|width|height|focal|fn|et|iso|"
           "has_gps|lat|lon|keywords|tags|description|artist|copyright|"
           "collection|rating|flagged\n";
    for (auto& [id, rec] : records_) {
        out << serialize(rec) << '\n';
    }
    return out.good();
}

uint64_t PhotoCatalog::addPhoto(const PhotoRecord& rec) {
    if (hasPath(rec.path)) {
        auto existing = findByPath(rec.path);
        if (existing) return existing->id;
    }
    PhotoRecord r = rec;
    r.id = next_id_++;
    if (r.date_imported.empty()) r.date_imported = currentDateStr();
    records_[r.id] = r;
    path_index_[r.path] = r.id;
    return r.id;
}

bool PhotoCatalog::updatePhoto(const PhotoRecord& rec) {
    auto it = records_.find(rec.id);
    if (it == records_.end()) return false;
    path_index_.erase(it->second.path);
    records_[rec.id] = rec;
    path_index_[rec.path] = rec.id;
    return true;
}

bool PhotoCatalog::removePhoto(uint64_t id) {
    auto it = records_.find(id);
    if (it == records_.end()) return false;
    path_index_.erase(it->second.path);
    records_.erase(it);
    return true;
}

bool PhotoCatalog::removeByPath(const std::string& path) {
    auto it = path_index_.find(path);
    if (it == path_index_.end()) return false;
    return removePhoto(it->second);
}

std::optional<PhotoRecord> PhotoCatalog::findById(uint64_t id) const {
    auto it = records_.find(id);
    if (it == records_.end()) return std::nullopt;
    return it->second;
}

std::optional<PhotoRecord> PhotoCatalog::findByPath(const std::string& path) const {
    auto it = path_index_.find(path);
    if (it == path_index_.end()) return std::nullopt;
    return findById(it->second);
}

bool PhotoCatalog::hasPath(const std::string& path) const {
    return path_index_.count(path) > 0;
}

std::vector<PhotoRecord> PhotoCatalog::getAll() const {
    std::vector<PhotoRecord> result;
    result.reserve(records_.size());
    for (auto& [id, rec] : records_) result.push_back(rec);
    std::sort(result.begin(), result.end(),
              [](const PhotoRecord& a, const PhotoRecord& b){
                  return a.date_taken > b.date_taken;
              });
    return result;
}

std::vector<PhotoRecord> PhotoCatalog::search(const SearchFilter& f) const {
    std::vector<PhotoRecord> result;
    for (auto& [id, rec] : records_) {
        if (!f.camera_make.empty() && !containsICase(rec.camera_make, f.camera_make)) continue;
        if (!f.camera_model.empty() && !containsICase(rec.camera_model, f.camera_model)) continue;
        if (!f.lens_model.empty() && !containsICase(rec.lens_model, f.lens_model)) continue;
        if (!f.keyword.empty() && !containsICase(rec.keywords, f.keyword) &&
            !containsICase(rec.tags, f.keyword) &&
            !containsICase(rec.description, f.keyword)) continue;
        if (!f.tag.empty() && !containsICase(rec.tags, f.tag)) continue;
        if (!f.collection.empty() && !containsICase(rec.collection, f.collection)) continue;
        if (!f.date_from.empty() && rec.date_taken < f.date_from) continue;
        if (!f.date_to.empty() && rec.date_taken > f.date_to) continue;
        if (f.min_rating >= 0 && rec.rating < f.min_rating) continue;
        if (f.flagged_only && !rec.flagged) continue;
        if (f.min_width > 0 && rec.image_width < f.min_width) continue;
        if (f.min_height > 0 && rec.image_height < f.min_height) continue;
        result.push_back(rec);
    }
    std::sort(result.begin(), result.end(),
              [](const PhotoRecord& a, const PhotoRecord& b){
                  return a.date_taken > b.date_taken;
              });
    return result;
}

std::vector<PhotoRecord> PhotoCatalog::getDuplicates() const {
    std::unordered_map<std::string, std::vector<uint64_t>> by_hash;
    for (auto& [id, rec] : records_) {
        if (!rec.hash.empty()) by_hash[rec.hash].push_back(id);
    }
    std::vector<PhotoRecord> result;
    for (auto& [hash, ids] : by_hash) {
        if (ids.size() > 1) {
            for (uint64_t id : ids) {
                auto it = records_.find(id);
                if (it != records_.end()) result.push_back(it->second);
            }
        }
    }
    return result;
}

std::vector<std::string> PhotoCatalog::getDistinctCameras() const {
    std::set<std::string> seen;
    for (auto& [id, rec] : records_)
        if (!rec.camera_model.empty())
            seen.insert(rec.camera_make + " " + rec.camera_model);
    return {seen.begin(), seen.end()};
}

std::vector<std::string> PhotoCatalog::getDistinctLenses() const {
    std::set<std::string> seen;
    for (auto& [id, rec] : records_)
        if (!rec.lens_model.empty()) seen.insert(rec.lens_model);
    return {seen.begin(), seen.end()};
}

std::vector<std::string> PhotoCatalog::getDistinctKeywords() const {
    std::set<std::string> seen;
    for (auto& [id, rec] : records_) {
        std::istringstream ss(rec.keywords);
        std::string kw;
        while (std::getline(ss, kw, ',')) {
            while (!kw.empty() && kw.front()==' ') kw.erase(kw.begin());
            while (!kw.empty() && kw.back()==' ')  kw.pop_back();
            if (!kw.empty()) seen.insert(kw);
        }
    }
    return {seen.begin(), seen.end()};
}

std::vector<std::string> PhotoCatalog::getDistinctCollections() const {
    std::set<std::string> seen;
    for (auto& [id, rec] : records_)
        if (!rec.collection.empty()) seen.insert(rec.collection);
    return {seen.begin(), seen.end()};
}

void PhotoCatalog::setRating(uint64_t id, int rating) {
    auto it = records_.find(id);
    if (it != records_.end()) it->second.rating = rating;
}

void PhotoCatalog::setFlagged(uint64_t id, bool flagged) {
    auto it = records_.find(id);
    if (it != records_.end()) it->second.flagged = flagged;
}

void PhotoCatalog::addKeyword(uint64_t id, const std::string& keyword) {
    auto it = records_.find(id);
    if (it == records_.end()) return;
    auto& kw = it->second.keywords;
    if (!kw.empty()) kw += ",";
    kw += keyword;
}

void PhotoCatalog::addTag(uint64_t id, const std::string& tag) {
    auto it = records_.find(id);
    if (it == records_.end()) return;
    auto& tags = it->second.tags;
    if (!tags.empty()) tags += ",";
    tags += tag;
}

void PhotoCatalog::setCollection(uint64_t id, const std::string& collection) {
    auto it = records_.find(id);
    if (it != records_.end()) it->second.collection = collection;
}

size_t PhotoCatalog::count() const { return records_.size(); }
void   PhotoCatalog::clear() { records_.clear(); path_index_.clear(); next_id_ = 1; }

PhotoRecord PhotoCatalog::fromExif(const std::string& path, uint64_t size,
                                    const std::string& hash,
                                    const Exif::ExifData& exif)
{
    PhotoRecord r{};
    r.path         = path;
    r.hash         = hash;
    r.file_size    = size;
    r.date_taken   = exif.datetime_original.empty()
                         ? exif.datetime : exif.datetime_original;
    r.camera_make  = exif.camera_make;
    r.camera_model = exif.camera_model;
    r.lens_model   = exif.lens_model.empty()
                         ? exif.lens_make : exif.lens_model;
    r.image_width  = exif.image_width;
    r.image_height = exif.image_height;
    r.focal_length = exif.focal_length;
    r.f_number     = exif.f_number;
    r.exposure_time= exif.exposure_time;
    r.iso          = exif.iso_speed;
    r.has_gps      = exif.has_gps;
    r.artist       = exif.artist;
    r.copyright    = exif.copyright;
    r.description  = exif.image_description;
    r.rating       = 0;
    r.flagged      = false;

    if (exif.has_gps) {
        if (exif.gps_latitude) r.gps_lat = exif.gps_latitude->toDecimal();
        if (exif.gps_longitude) r.gps_lon= exif.gps_longitude->toDecimal();
    }
    return r;
}

} // namespace PhotoManager::Catalog