#include "search_engine.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <vector>

namespace PhotoManager::Filter {

SearchEngine::SearchEngine(const Catalog::PhotoCatalog& catalog)
    : catalog_(catalog)
{}

bool SearchEngine::matchesText(const Catalog::PhotoRecord& rec,
                                const std::string& text)
{
    if (text.empty()) return true;
    auto ci = [](const std::string& h, const std::string& n) {
        return std::search(h.begin(), h.end(), n.begin(), n.end(),
                           [](char a, char b){ return std::tolower(a)==std::tolower(b); })
               != h.end();
    };
    return ci(rec.camera_make,  text) || ci(rec.camera_model, text) ||
           ci(rec.lens_model,   text) || ci(rec.keywords,     text) ||
           ci(rec.tags,         text) || ci(rec.description,  text) ||
           ci(rec.artist,       text) || ci(rec.collection,   text) ||
           ci(rec.path,         text);
}

double SearchEngine::haversineKm(double lat1, double lon1,
                                  double lat2, double lon2)
{
    constexpr double R = 6371.0;
    auto rad = [](double d){ return d * 3.14159265358979323846 / 180.0; };
    double dlat = rad(lat2 - lat1);
    double dlon = rad(lon2 - lon1);
    double a = std::sin(dlat/2) * std::sin(dlat/2) +
               std::cos(rad(lat1)) * std::cos(rad(lat2)) *
               std::sin(dlon/2) * std::sin(dlon/2);
    return R * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1-a));
}

std::vector<Catalog::PhotoRecord> SearchEngine::sortResults(
    std::vector<Catalog::PhotoRecord> records,
    SortField field, SortOrder order)
{
    auto cmp = [&](const Catalog::PhotoRecord& a, const Catalog::PhotoRecord& b) {
        bool less;
        switch (field) {
        case SortField::DATE_TAKEN:
            less = a.date_taken < b.date_taken; break;
        case SortField::DATE_IMPORTED:
            less = a.date_imported < b.date_imported; break;
        case SortField::FILE_NAME:
            less = a.path < b.path; break;
        case SortField::FILE_SIZE:
            less = a.file_size < b.file_size; break;
        case SortField::CAMERA:
            less = (a.camera_make + a.camera_model) < (b.camera_make + b.camera_model); break;
        case SortField::RATING:
            less = a.rating < b.rating; break;
        case SortField::FOCAL_LENGTH:
            less = a.focal_length < b.focal_length; break;
        case SortField::ISO:
            less = a.iso < b.iso; break;
        default:
            less = a.date_taken < b.date_taken; break;
        }
        return order == SortOrder::ASCENDING ? less : !less;
    };
    std::sort(records.begin(), records.end(), cmp);
    return records;
}

SearchResult SearchEngine::search(const SearchQuery& query) const {
    auto t0 = std::chrono::high_resolution_clock::now();

    auto filtered = catalog_.search(query.filter);

    if (!query.text.empty()) {
        filtered.erase(
            std::remove_if(filtered.begin(), filtered.end(),
                [&](const Catalog::PhotoRecord& r){
                    return !matchesText(r, query.text);
                }),
            filtered.end());
    }

    filtered = sortResults(filtered, query.sort_by, query.sort_order);
    size_t total = filtered.size();

    if (query.offset < filtered.size())
        filtered.erase(filtered.begin(),
                        filtered.begin() + static_cast<ptrdiff_t>(query.offset));
    else
        filtered.clear();

    if (query.limit > 0 && filtered.size() > query.limit)
        filtered.resize(query.limit);

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1-t0).count();

    return {filtered, total, ms};
}

SearchResult SearchEngine::fullTextSearch(const std::string& text) const {
    SearchQuery q;
    q.text = text;
    return search(q);
}

SearchResult SearchEngine::findByCamera(const std::string& camera) const {
    SearchQuery q;
    q.filter.camera_model = camera;
    return search(q);
}

SearchResult SearchEngine::findByLens(const std::string& lens) const {
    SearchQuery q;
    q.filter.lens_model = lens;
    return search(q);
}

SearchResult SearchEngine::findByDateRange(const std::string& from,
                                            const std::string& to) const
{
    SearchQuery q;
    q.filter.date_from = from;
    q.filter.date_to   = to;
    return search(q);
}

SearchResult SearchEngine::findByKeyword(const std::string& keyword) const {
    SearchQuery q;
    q.filter.keyword = keyword;
    return search(q);
}

SearchResult SearchEngine::findByCollection(const std::string& collection) const {
    SearchQuery q;
    q.filter.collection = collection;
    return search(q);
}

SearchResult SearchEngine::findDuplicates() const {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto dups = catalog_.getDuplicates();
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1-t0).count();
    return {dups, dups.size(), ms};
}

SearchResult SearchEngine::findByGpsRadius(double lat, double lon,
                                            double radius_km) const
{
    auto t0 = std::chrono::high_resolution_clock::now();
    auto all = catalog_.getAll();
    std::vector<Catalog::PhotoRecord> result;
    for (auto& rec : all) {
        if (!rec.has_gps) continue;
        double d = haversineKm(lat, lon, rec.gps_lat, rec.gps_lon);
        if (d <= radius_km) result.push_back(rec);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1-t0).count();
    return {result, result.size(), ms};
}

std::vector<std::string> SearchEngine::suggestKeywords(const std::string& prefix) const {
    auto all = catalog_.getDistinctKeywords();
    std::vector<std::string> result;
    for (auto& kw : all) {
        if (prefix.empty() || kw.substr(0, prefix.size()) == prefix)
            result.push_back(kw);
    }
    if (result.size() > 10) result.resize(10);
    return result;
}

std::vector<std::string> SearchEngine::suggestCameras(const std::string& prefix) const {
    auto all = catalog_.getDistinctCameras();
    std::vector<std::string> result;
    for (auto& c : all) {
        std::string low = c;
        std::string plow = prefix;
        for (auto& ch : low) ch = static_cast<char>(std::tolower(ch));
        for (auto& ch : plow) ch = static_cast<char>(std::tolower(ch));
        if (prefix.empty() || low.find(plow) != std::string::npos)
            result.push_back(c);
    }
    return result;
}

} // namespace PhotoManager::Filter