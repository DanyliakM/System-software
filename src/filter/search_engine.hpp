#pragma once
#include "../catalog/catalog.hpp"
#include <string>
#include <vector>
#include <functional>

namespace PhotoManager::Filter {

enum class SortField {
    DATE_TAKEN,
    DATE_IMPORTED,
    FILE_NAME,
    FILE_SIZE,
    CAMERA,
    RATING,
    FOCAL_LENGTH,
    ISO
};

enum class SortOrder {
    ASCENDING,
    DESCENDING
};

struct SearchQuery {
    std::string              text;
    Catalog::SearchFilter    filter;
    SortField                sort_by    = SortField::DATE_TAKEN;
    SortOrder                sort_order = SortOrder::DESCENDING;
    size_t                   limit      = 0;
    size_t                   offset     = 0;
};

struct SearchResult {
    std::vector<Catalog::PhotoRecord> photos;
    size_t                            total_count;
    double                            elapsed_ms;
};

class SearchEngine {
public:
    explicit SearchEngine(const Catalog::PhotoCatalog& catalog);

    SearchResult search(const SearchQuery& query) const;
    SearchResult fullTextSearch(const std::string& text) const;
    SearchResult findByCamera(const std::string& camera) const;
    SearchResult findByLens(const std::string& lens) const;
    SearchResult findByDateRange(const std::string& from,
                                  const std::string& to) const;
    SearchResult findByKeyword(const std::string& keyword) const;
    SearchResult findByCollection(const std::string& collection) const;
    SearchResult findDuplicates() const;
    SearchResult findByGpsRadius(double lat, double lon,
                                  double radius_km) const;

    std::vector<std::string> suggestKeywords(const std::string& prefix) const;
    std::vector<std::string> suggestCameras(const std::string& prefix) const;

    static std::vector<Catalog::PhotoRecord> sortResults(
        std::vector<Catalog::PhotoRecord> records,
        SortField field, SortOrder order);

private:
    static double haversineKm(double lat1, double lon1,
                               double lat2, double lon2);
    static bool matchesText(const Catalog::PhotoRecord& rec,
                             const std::string& text);

    const Catalog::PhotoCatalog& catalog_;
};

} // namespace PhotoManager::Filter