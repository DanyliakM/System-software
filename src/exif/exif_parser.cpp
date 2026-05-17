#include "exif_parser.hpp"
#include "../filesystem/file_ops.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <cstdint>
#include <vector>
#include <filesystem>
#include <cmath>

namespace PhotoManager::Exif {

namespace {

std::vector<uint8_t> readFile(const std::wstring& path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    size_t sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    return buf;
}

bool writeFile(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    return f.good();
}

constexpr uint16_t TAG_MAKE            = 0x010F;
constexpr uint16_t TAG_MODEL           = 0x0110;
constexpr uint16_t TAG_ORIENTATION     = 0x0112;
constexpr uint16_t TAG_XRESOLUTION     = 0x011A;
constexpr uint16_t TAG_YRESOLUTION     = 0x011B;
constexpr uint16_t TAG_SOFTWARE        = 0x0131;
constexpr uint16_t TAG_DATETIME        = 0x0132;
constexpr uint16_t TAG_ARTIST          = 0x013B;
constexpr uint16_t TAG_COPYRIGHT       = 0x8298;
constexpr uint16_t TAG_IMGDESC         = 0x010E;
constexpr uint16_t TAG_EXIF_IFD        = 0x8769;
constexpr uint16_t TAG_GPS_IFD         = 0x8825;
constexpr uint16_t TAG_IMAGE_WIDTH     = 0xA002;
constexpr uint16_t TAG_IMAGE_HEIGHT    = 0xA003;
constexpr uint16_t TAG_PIXEL_X_DIM    = 0xA002;
constexpr uint16_t TAG_PIXEL_Y_DIM    = 0xA003;
constexpr uint16_t TAG_EXPOSURE_TIME   = 0x829A;
constexpr uint16_t TAG_FNUMBER         = 0x829D;
constexpr uint16_t TAG_EXPOSURE_PROG   = 0x8822;
constexpr uint16_t TAG_ISO             = 0x8827;
constexpr uint16_t TAG_DT_ORIGINAL    = 0x9003;
constexpr uint16_t TAG_DT_DIGITIZED   = 0x9004;
constexpr uint16_t TAG_EXPOSURE_BIAS  = 0x9204;
constexpr uint16_t TAG_MAX_APERTURE   = 0x9205;
constexpr uint16_t TAG_METERING       = 0x9207;
constexpr uint16_t TAG_FLASH          = 0x9209;
constexpr uint16_t TAG_FOCAL_LEN      = 0x920A;
constexpr uint16_t TAG_USER_COMMENT   = 0x9286;
constexpr uint16_t TAG_FOCAL_LEN_35   = 0xA405;
constexpr uint16_t TAG_DIGITAL_ZOOM   = 0xA404;
constexpr uint16_t TAG_LENS_MAKE      = 0xA433;
constexpr uint16_t TAG_LENS_MODEL     = 0xA434;

constexpr uint16_t GPS_LATREF         = 0x0001;
constexpr uint16_t GPS_LAT            = 0x0002;
constexpr uint16_t GPS_LONREF         = 0x0003;
constexpr uint16_t GPS_LON            = 0x0004;
constexpr uint16_t GPS_ALTREF         = 0x0005;
constexpr uint16_t GPS_ALT            = 0x0006;
constexpr uint16_t GPS_DATETIME       = 0x001D;

} // anonymous namespace

uint16_t ExifParser::TiffContext::readU16(uint32_t offset) const {
    if (offset + 2 > data.size()) return 0;
    uint16_t v;
    std::memcpy(&v, data.data() + offset, 2);
    if (!little_endian) {
        v = static_cast<uint16_t>((v>>8) | (v<<8));
    }
    return v;
}

uint32_t ExifParser::TiffContext::readU32(uint32_t offset) const {
    if (offset + 4 > data.size()) return 0;
    uint32_t v;
    std::memcpy(&v, data.data() + offset, 4);
    if (!little_endian) {
        v = ((v>>24)&0xFF) | ((v>>8)&0xFF00) |
            ((v<<8)&0xFF0000) | ((v<<24)&0xFF000000u);
    }
    return v;
}

int32_t ExifParser::TiffContext::readS32(uint32_t offset) const {
    return static_cast<int32_t>(readU32(offset));
}

URational ExifParser::TiffContext::readURational(uint32_t offset) const {
    URational r;
    r.numerator   = readU32(offset);
    r.denominator = readU32(offset + 4);
    return r;
}

Rational ExifParser::TiffContext::readRational(uint32_t offset) const {
    Rational r;
    r.numerator   = readS32(offset);
    r.denominator = readS32(offset + 4);
    return r;
}

std::string ExifParser::TiffContext::readAscii(uint32_t offset, uint32_t count) const {
    if (offset + count > data.size()) return {};
    std::string s(reinterpret_cast<const char*>(data.data() + offset), count);
    while (!s.empty() && (s.back() == '\0' || s.back() == ' '))
        s.pop_back();
    return s;
}

bool ExifParser::findApp1(const std::vector<uint8_t>& jpeg,
                           size_t& app1_offset, size_t& app1_size)
{
    if (jpeg.size() < 4) return false;
    if (jpeg[0] != 0xFF || jpeg[1] != 0xD8) return false;

    size_t pos = 2;
    while (pos + 4 <= jpeg.size()) {
        if (jpeg[pos] != 0xFF) return false;
        uint8_t marker = jpeg[pos+1];
        uint16_t len = static_cast<uint16_t>((jpeg[pos+2] << 8) | jpeg[pos+3]);
        if (marker == 0xE1) {
            if (pos + 6 + 6 <= jpeg.size()) {
                const char* sig = reinterpret_cast<const char*>(jpeg.data() + pos + 4);
                if (std::memcmp(sig, "Exif\0\0", 6) == 0) {
                    app1_offset = pos + 10;
                    app1_size   = len - 8;
                    return true;
                }
            }
        }
        if (marker == 0xDA) break;
        pos += 2 + len;
    }
    return false;
}

bool ExifParser::findApp13(const std::vector<uint8_t>& jpeg,
                            size_t& offset, size_t& size)
{
    if (jpeg.size() < 4) return false;
    size_t pos = 2;
    while (pos + 4 <= jpeg.size()) {
        if (jpeg[pos] != 0xFF) return false;
        uint8_t marker = jpeg[pos+1];
        uint16_t len = static_cast<uint16_t>((jpeg[pos+2] << 8) | jpeg[pos+3]);
        if (marker == 0xED) {
            offset = pos + 4;
            size   = len - 2;
            return true;
        }
        if (marker == 0xDA) break;
        pos += 2 + len;
    }
    return false;
}

void ExifParser::parseGpsIfd(const TiffContext& ctx, uint32_t ifd_offset,
                              ExifData& out)
{
    if (ifd_offset + 2 > ctx.data.size()) return;
    uint16_t count = ctx.readU16(ifd_offset);
    uint32_t pos   = ifd_offset + 2;

    std::string lat_ref, lon_ref;
    std::array<URational,3> lat_val{}, lon_val{};
    bool has_lat = false, has_lon = false;

    for (uint16_t i = 0; i < count && pos + 12 <= ctx.data.size(); ++i, pos += 12) {
        uint16_t tag   = ctx.readU16(pos);
        uint16_t type  = ctx.readU16(pos+2);
        uint32_t n     = ctx.readU32(pos+4);
        uint32_t voff  = ctx.readU32(pos+8);

        (void)type;
        (void)n;

        if (tag == GPS_LATREF) {
            lat_ref = std::string(1, static_cast<char>(ctx.data[pos+8]));
        } else if (tag == GPS_LONREF) {
            lon_ref = std::string(1, static_cast<char>(ctx.data[pos+8]));
        } else if (tag == GPS_LAT) {
            for (int k = 0; k < 3; ++k)
                lat_val[k] = ctx.readURational(voff + k*8);
            has_lat = true;
        } else if (tag == GPS_LON) {
            for (int k = 0; k < 3; ++k)
                lon_val[k] = ctx.readURational(voff + k*8);
            has_lon = true;
        } else if (tag == GPS_ALT) {
            URational alt = ctx.readURational(voff);
            out.gps_altitude = alt.toDouble();
            out.has_gps = true;
        } else if (tag == GPS_DATETIME) {
            out.gps_datetime = ctx.readAscii(voff, 11);
        }
    }

    if (has_lat && !lat_ref.empty()) {
        GpsCoord c;
        c.degrees = lat_val[0].toDouble();
        c.minutes = lat_val[1].toDouble();
        c.seconds = lat_val[2].toDouble();
        c.ref     = lat_ref.empty() ? 'N' : lat_ref[0];
        out.gps_latitude = c;
        out.has_gps = true;
    }
    if (has_lon && !lon_ref.empty()) {
        GpsCoord c;
        c.degrees = lon_val[0].toDouble();
        c.minutes = lon_val[1].toDouble();
        c.seconds = lon_val[2].toDouble();
        c.ref     = lon_ref.empty() ? 'E' : lon_ref[0];
        out.gps_longitude = c;
        out.has_gps = true;
    }
}

void ExifParser::parseExifIfd(const TiffContext& ctx, uint32_t ifd_offset,
                               ExifData& out)
{
    if (ifd_offset + 2 > ctx.data.size()) return;
    uint16_t count = ctx.readU16(ifd_offset);
    uint32_t pos   = ifd_offset + 2;

    for (uint16_t i = 0; i < count && pos + 12 <= ctx.data.size(); ++i, pos += 12) {
        uint16_t tag  = ctx.readU16(pos);
        uint16_t type = ctx.readU16(pos+2);
        uint32_t n    = ctx.readU32(pos+4);
        uint32_t vraw = ctx.readU32(pos+8);

        uint32_t voff = vraw;
        uint32_t type_size[] = {0,1,1,2,4,8,1,1,2,4,8,4,8};
        uint32_t data_size = (type < 13) ? type_size[type] * n : 0;
        if (data_size <= 4) {
            if (ctx.little_endian) voff = pos + 8;
            else {
                if (data_size == 1) voff = pos + 8;
                else if (data_size == 2) voff = pos + 8;
                else voff = pos + 8;
            }
        }

        switch (tag) {
        case TAG_EXPOSURE_TIME:
            if (type == 5) out.exposure_time = ctx.readURational(vraw).toDouble();
            break;
        case TAG_FNUMBER:
            if (type == 5) out.f_number = ctx.readURational(vraw).toDouble();
            break;
        case TAG_EXPOSURE_PROG:
            out.exposure_program = ctx.readU16(pos+8);
            break;
        case TAG_ISO:
            out.iso_speed = ctx.readU16(pos+8);
            break;
        case TAG_DT_ORIGINAL:
            if (type == 2) out.datetime_original = ctx.readAscii(vraw, n);
            break;
        case TAG_DT_DIGITIZED:
            if (type == 2) out.datetime_digitized = ctx.readAscii(vraw, n);
            break;
        case TAG_EXPOSURE_BIAS:
            if (type == 10) out.exposure_bias = ctx.readRational(vraw).toDouble();
            break;
        case TAG_METERING:
            out.metering_mode = ctx.readU16(pos+8);
            break;
        case TAG_FLASH:
            out.flash = ctx.readU16(pos+8);
            break;
        case TAG_FOCAL_LEN:
            if (type == 5) out.focal_length = ctx.readURational(vraw).toDouble();
            break;
        case TAG_USER_COMMENT:
            if (n > 8 && vraw + n <= ctx.data.size()) {
                std::string enc(reinterpret_cast<const char*>(ctx.data.data()+vraw), 8);
                std::string txt(reinterpret_cast<const char*>(ctx.data.data()+vraw+8), n-8);
                while (!txt.empty() && (txt.back()=='\0'||txt.back()==' ')) txt.pop_back();
                out.user_comment = txt;
            }
            break;
        case TAG_PIXEL_X_DIM:
            if (type == 3) out.image_width = ctx.readU16(pos+8);
            else if (type == 4) out.image_width = vraw;
            break;
        case TAG_PIXEL_Y_DIM:
            if (type == 3) out.image_height = ctx.readU16(pos+8);
            else if (type == 4) out.image_height = vraw;
            break;
        case TAG_FOCAL_LEN_35:
            out.focal_length_35 = ctx.readU16(pos+8);
            break;
        case TAG_DIGITAL_ZOOM:
            if (type == 5) out.digital_zoom_ratio = ctx.readURational(vraw).toDouble();
            break;
        case TAG_LENS_MAKE:
            if (type == 2 && n > 1) out.lens_make = ctx.readAscii(vraw, n);
            break;
        case TAG_LENS_MODEL:
            if (type == 2 && n > 1) out.lens_model = ctx.readAscii(vraw, n);
            break;
        default:
            break;
        }
        (void)voff;
    }
}

void ExifParser::parseIfd(const TiffContext& ctx, uint32_t ifd_offset,
                           ExifData& out)
{
    if (ifd_offset + 2 > ctx.data.size()) return;
    uint16_t count = ctx.readU16(ifd_offset);
    uint32_t pos   = ifd_offset + 2;

    for (uint16_t i = 0; i < count && pos + 12 <= ctx.data.size(); ++i, pos += 12) {
        uint16_t tag  = ctx.readU16(pos);
        uint16_t type = ctx.readU16(pos+2);
        uint32_t n    = ctx.readU32(pos+4);
        uint32_t vraw = ctx.readU32(pos+8);

        switch (tag) {
        case TAG_IMGDESC:
            if (type == 2 && n > 1) out.image_description = ctx.readAscii(vraw, n);
            break;
        case TAG_MAKE:
            if (type == 2 && n > 1) out.camera_make = ctx.readAscii(vraw, n);
            break;
        case TAG_MODEL:
            if (type == 2 && n > 1) out.camera_model = ctx.readAscii(vraw, n);
            break;
        case TAG_ORIENTATION:
            out.orientation = ctx.readU16(pos+8);
            break;
        case TAG_SOFTWARE:
            if (type == 2 && n > 1) out.software = ctx.readAscii(vraw, n);
            break;
        case TAG_DATETIME:
            if (type == 2) out.datetime = ctx.readAscii(vraw, n);
            break;
        case TAG_ARTIST:
            if (type == 2) out.artist = ctx.readAscii(vraw, n);
            break;
        case TAG_COPYRIGHT:
            if (type == 2) out.copyright = ctx.readAscii(vraw, n);
            break;
        case TAG_EXIF_IFD:
            parseExifIfd(ctx, vraw, out);
            break;
        case TAG_GPS_IFD:
            parseGpsIfd(ctx, vraw, out);
            break;
        default:
            break;
        }
        (void)type;
    }
}

std::optional<ExifData> ExifParser::parse(const std::wstring& jpeg_path) {
    auto jpeg = readFile(jpeg_path);
    if (jpeg.size() < 12) return std::nullopt;

    size_t app1_offset, app1_size;
    if (!findApp1(jpeg, app1_offset, app1_size)) return std::nullopt;
    if (app1_offset + app1_size > jpeg.size()) return std::nullopt;

    TiffContext ctx;
    ctx.data.assign(jpeg.begin() + app1_offset,
                    jpeg.begin() + app1_offset + app1_size);

    if (ctx.data.size() < 8) return std::nullopt;
    if (ctx.data[0] == 'I' && ctx.data[1] == 'I') ctx.little_endian = true;
    else if (ctx.data[0] == 'M' && ctx.data[1] == 'M') ctx.little_endian = false;
    else return std::nullopt;

    ctx.base_offset = 0;
    uint32_t ifd0_off = ctx.readU32(4);
    if (ifd0_off + 2 > ctx.data.size()) return std::nullopt;

    ExifData out;
    parseIfd(ctx, ifd0_off, out);
    return out;
}

std::optional<IptcData> ExifParser::parseIptc(const std::wstring& jpeg_path) {
    auto jpeg = readFile(jpeg_path);
    size_t offset, size;
    if (!findApp13(jpeg, offset, size)) return std::nullopt;

    const uint8_t* p   = jpeg.data() + offset;
    const uint8_t* end = p + size;

    static const char IPTC_HDR[] = "Photoshop 3.0";
    if (size < 14 || std::memcmp(p, IPTC_HDR, 13) != 0) return std::nullopt;
    p += 14;

    IptcData iptc;
    while (p + 5 <= end) {
        if (p[0] != 0x1C) { ++p; continue; }
        uint8_t  rec = p[1];
        uint8_t  ds  = p[2];
        uint16_t len = static_cast<uint16_t>((p[3] << 8) | p[4]);
        p += 5;
        if (p + len > end) break;

        if (rec == 2) {
            std::string val(reinterpret_cast<const char*>(p), len);
            switch (ds) {
            case 5:  iptc.object_name  = val; break;
            case 25: iptc.keywords    += (iptc.keywords.empty() ? "" : ",") + val; break;
            case 80: iptc.byline      = val; break;
            case 85: iptc.credit      = val; break;
            case 110: iptc.caption    = val; break;
            case 116: iptc.copyright  = val; break;
            case 90: iptc.city        = val; break;
            case 101: iptc.country    = val; break;
            case 55: iptc.date_created= val; break;
            }
        }
        p += len;
    }
    return iptc;
}

static bool patchExifString(std::vector<uint8_t>& jpeg,
                              uint16_t target_tag,
                              const std::string& new_val)
{
    size_t app1_offset, app1_size;
    if (!ExifParser::findApp1(jpeg, app1_offset, app1_size)) return false;
    (void)app1_size;
    (void)target_tag;
    (void)new_val;
    return false;
}

bool ExifParser::writeArtist(const std::wstring& path, const std::string& artist) {
    auto jpeg = readFile(path);
    if (jpeg.empty()) return false;
    return patchExifString(jpeg, TAG_ARTIST, artist) && writeFile(path, jpeg);
}

bool ExifParser::writeCopyright(const std::wstring& path, const std::string& copyright) {
    auto jpeg = readFile(path);
    if (jpeg.empty()) return false;
    return patchExifString(jpeg, TAG_COPYRIGHT, copyright) && writeFile(path, jpeg);
}

bool ExifParser::writeUserComment(const std::wstring& path, const std::string& comment) {
    auto jpeg = readFile(path);
    if (jpeg.empty()) return false;
    return patchExifString(jpeg, TAG_USER_COMMENT, comment) && writeFile(path, jpeg);
}

bool ExifParser::writeDateTime(const std::wstring& path, const std::string& datetime) {
    auto jpeg = readFile(path);
    if (jpeg.empty()) return false;
    return patchExifString(jpeg, TAG_DATETIME, datetime) && writeFile(path, jpeg);
}

std::string ExifParser::formatDateTime(const std::string& exif_dt) {
    if (exif_dt.size() < 19) return exif_dt;
    return exif_dt.substr(0,4) + "-" + exif_dt.substr(5,2) + "-" +
           exif_dt.substr(8,2) + " " + exif_dt.substr(11,8);
}

std::string ExifParser::exposureTimeStr(double et) {
    if (et <= 0.0) return "?";
    if (et >= 1.0) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << et << "s";
        return ss.str();
    }
    int denom = static_cast<int>(std::round(1.0 / et));
    return "1/" + std::to_string(denom) + "s";
}

std::string ExifParser::fNumberStr(double fn) {
    if (fn <= 0.0) return "?";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << fn;
    return "f/" + ss.str();
}

} // namespace PhotoManager::Exif