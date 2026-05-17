#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace PhotoManager::Exif {

enum class ExifType : uint16_t {
    BYTE      = 1,
    ASCII     = 2,
    SHORT     = 3,
    LONG      = 4,
    RATIONAL  = 5,
    SBYTE     = 6,
    UNDEFINED = 7,
    SSHORT    = 8,
    SLONG     = 9,
    SRATIONAL = 10,
    FLOAT     = 11,
    DOUBLE    = 12
};

struct Rational {
    int32_t numerator;
    int32_t denominator;
    double toDouble() const {
        return denominator != 0
            ? static_cast<double>(numerator) / denominator
            : 0.0;
    }
};

struct URational {
    uint32_t numerator;
    uint32_t denominator;
    double toDouble() const {
        return denominator != 0
            ? static_cast<double>(numerator) / denominator
            : 0.0;
    }
};

struct GpsCoord {
    double degrees;
    double minutes;
    double seconds;
    char   ref;

    double toDecimal() const {
        double d = degrees + minutes/60.0 + seconds/3600.0;
        return (ref=='S' || ref=='W') ? -d : d;
    }
};

struct ExifData {
    std::string camera_make;
    std::string camera_model;
    std::string lens_make;
    std::string lens_model;
    std::string software;
    std::string datetime_original;
    std::string datetime_digitized;
    std::string datetime;
    std::string artist;
    std::string copyright;
    std::string image_description;
    std::string user_comment;

    uint32_t image_width  = 0;
    uint32_t image_height = 0;

    double exposure_time    = 0.0;
    double f_number         = 0.0;
    double focal_length     = 0.0;
    double focal_length_35  = 0.0;
    int32_t iso_speed       = 0;
    int32_t exposure_program= 0;
    int32_t metering_mode   = 0;
    int32_t flash           = 0;
    int32_t orientation     = 1;
    double  digital_zoom_ratio = 1.0;
    double  exposure_bias   = 0.0;

    bool has_gps = false;
    std::optional<GpsCoord> gps_latitude;
    std::optional<GpsCoord> gps_longitude;
    std::optional<double>   gps_altitude;
    std::string             gps_datetime;

    std::unordered_map<uint16_t, std::string> raw_tags;

    bool empty() const { return camera_make.empty() && camera_model.empty(); }
};

struct IptcData {
    std::string object_name;
    std::string caption;
    std::string keywords;
    std::string category;
    std::string city;
    std::string country;
    std::string credit;
    std::string source;
    std::string copyright;
    std::string byline;
    std::string date_created;
};

class ExifParser {
public:
    static std::optional<ExifData> parse(const std::wstring& jpeg_path);
    static std::optional<IptcData> parseIptc(const std::wstring& jpeg_path);

    static bool writeArtist(const std::wstring& jpeg_path, const std::string& artist);
    static bool writeCopyright(const std::wstring& jpeg_path, const std::string& copyright);
    static bool writeUserComment(const std::wstring& jpeg_path, const std::string& comment);
    static bool writeDateTime(const std::wstring& jpeg_path, const std::string& datetime);

    static std::string formatDateTime(const std::string& exif_dt);
    static std::string exposureTimeStr(double et);
    static std::string fNumberStr(double fn);
    static bool findApp1(const std::vector<uint8_t>& jpeg,
                          size_t& app1_offset, size_t& app1_size);
private:
    struct TiffContext {
        std::vector<uint8_t> data;
        bool little_endian;
        uint32_t base_offset;

        uint16_t readU16(uint32_t offset) const;
        uint32_t readU32(uint32_t offset) const;
        int32_t  readS32(uint32_t offset) const;
        URational readURational(uint32_t offset) const;
        Rational  readRational(uint32_t offset) const;
        std::string readAscii(uint32_t offset, uint32_t count) const;
    };


    static bool findApp13(const std::vector<uint8_t>& jpeg,
                           size_t& offset, size_t& size);

    static void parseIfd(const TiffContext& ctx, uint32_t ifd_offset,
                          ExifData& out);
    static void parseExifIfd(const TiffContext& ctx, uint32_t ifd_offset,
                              ExifData& out);
    static void parseGpsIfd(const TiffContext& ctx, uint32_t ifd_offset,
                             ExifData& out);
};

} // namespace PhotoManager::Exif