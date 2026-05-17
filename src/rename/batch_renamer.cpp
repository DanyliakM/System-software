#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "batch_renamer.hpp"
#include "../filesystem/file_ops.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <unordered_set>

namespace PhotoManager::Rename {

RenameTemplate RenameTemplate::byDate() {
    return {"{YYYY}{MM}{DD}_{HH}{mm}{ss}", "lower"};
}

RenameTemplate RenameTemplate::byDateAndCamera() {
    return {"{YYYY}-{MM}-{DD}_{CAMERA}_{SEQ:4}", "lower"};
}

RenameTemplate RenameTemplate::bySequence(const std::string& prefix) {
    return {prefix + "_{SEQ:4}", "lower"};
}

RenameTemplate RenameTemplate::byEvent(const std::string& event) {
    return {event + "_{YYYY}{MM}{DD}_{SEQ:4}", "lower"};
}

RenameTemplate RenameTemplate::fromString(const std::string& pattern) {
    return {pattern, "lower"};
}

std::vector<std::string> BatchRenamer::availableTokens() {
    return {
        "{YYYY}      - 4-digit year",
        "{YY}        - 2-digit year",
        "{MM}        - month (01-12)",
        "{DD}        - day (01-31)",
        "{HH}        - hour (00-23)",
        "{mm}        - minute (00-59)",
        "{ss}        - second (00-59)",
        "{CAMERA}    - camera model (sanitized)",
        "{MAKE}      - camera make (sanitized)",
        "{LENS}      - lens model (sanitized)",
        "{ISO}       - ISO speed",
        "{FL}        - focal length (mm)",
        "{SEQ:N}     - sequence number padded to N digits",
        "{STEM}      - original filename without extension",
        "{EXT}       - original extension",
        "{ARTIST}    - artist/photographer name",
        "{KEYWORD1}  - first keyword",
        "{COLLECTION}- collection name"
    };
}

void BatchRenamer::printTemplateHelp() {
    std::cout << "\nAvailable template tokens:\n";
    for (auto& t : availableTokens())
        std::cout << "  " << t << '\n';
    std::cout << "\nExamples:\n"
              << "  {YYYY}{MM}{DD}_{HH}{mm}{ss}         -> 20240115_143022.jpg\n"
              << "  {YYYY}-{MM}-{DD}_{CAMERA}_{SEQ:4}   -> 2024-01-15_A7IV_0001.jpg\n"
              << "  vacation_{YYYY}{MM}{DD}_{SEQ:3}      -> vacation_20240115_001.jpg\n\n";
}

std::string BatchRenamer::padded(int n, int width) const {
    std::ostringstream ss;
    ss << std::setw(width) << std::setfill('0') << n;
    return ss.str();
}

std::string BatchRenamer::safeNamePart(const std::string& s) const {
    std::string r;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c=='-') r += c;
        else if (c==' ' || c=='_') r += '_';
    }
    while (!r.empty() && r.back()=='_') r.pop_back();
    return r;
}

std::string BatchRenamer::expandToken(const std::string& token,
                                       const Catalog::PhotoRecord& rec,
                                       int seq) const
{
    std::string dt = rec.date_taken;
    auto getdt = [&](int pos, int len, int def) -> int {
        if (static_cast<int>(dt.size()) > pos+len-1) {
            try { return std::stoi(dt.substr(pos, len)); }
            catch (...) {}
        }
        return def;
    };

    if (token == "YYYY") return dt.size()>=4 ? dt.substr(0,4) : "0000";
    if (token == "YY")   return dt.size()>=4 ? dt.substr(2,2) : "00";
    if (token == "MM")   return padded(getdt(5,2,1), 2);
    if (token == "DD")   return padded(getdt(8,2,1), 2);
    if (token == "HH")   return padded(getdt(11,2,0), 2);
    if (token == "mm")   return padded(getdt(14,2,0), 2);
    if (token == "ss")   return padded(getdt(17,2,0), 2);
    if (token == "CAMERA") return safeNamePart(rec.camera_model.empty() ? "UNKNOWN" : rec.camera_model);
    if (token == "MAKE")   return safeNamePart(rec.camera_make.empty() ? "UNKNOWN" : rec.camera_make);
    if (token == "LENS")   return safeNamePart(rec.lens_model.empty() ? "NOLENS" : rec.lens_model);
    if (token == "ISO")    return std::to_string(rec.iso);
    if (token == "FL")     return std::to_string(static_cast<int>(rec.focal_length));
    if (token == "ARTIST") return safeNamePart(rec.artist);
    if (token == "COLLECTION") return safeNamePart(rec.collection);
    if (token == "STEM") {
        std::string p = rec.path;
        size_t sep = p.rfind('\\');
        if (sep == std::string::npos) sep = p.rfind('/');
        std::string name = (sep != std::string::npos) ? p.substr(sep+1) : p;
        size_t dot = name.rfind('.');
        return dot != std::string::npos ? name.substr(0, dot) : name;
    }
    if (token == "EXT") {
        size_t dot = rec.path.rfind('.');
        return dot != std::string::npos ? rec.path.substr(dot+1) : "";
    }
    if (token.find("SEQ:") == 0) {
        int width = 4;
        try { width = std::stoi(token.substr(4)); } catch (...) {}
        return padded(seq, width);
    }
    if (token == "SEQ") return padded(seq, 4);
    if (token == "KEYWORD1") {
        std::string kw = rec.keywords;
        size_t comma = kw.find(',');
        return safeNamePart(comma != std::string::npos ? kw.substr(0, comma) : kw);
    }
    return "?" + token + "?";
}

std::string BatchRenamer::applyTemplate(const RenameTemplate& tmpl,
                                         const Catalog::PhotoRecord& rec,
                                         int seq) const
{
    std::string result;
    std::string pattern = tmpl.pattern;
    size_t i = 0;
    while (i < pattern.size()) {
        if (pattern[i] == '{') {
            size_t end = pattern.find('}', i+1);
            if (end == std::string::npos) { result += pattern[i++]; continue; }
            std::string token = pattern.substr(i+1, end-i-1);
            result += expandToken(token, rec, seq);
            i = end + 1;
        } else {
            result += pattern[i++];
        }
    }

    for (char c : {'"','*','?','<','>',':', '|'})
        std::replace(result.begin(), result.end(), c, '_');
    while (!result.empty() && (result.back()=='.'||result.back()==' '))
        result.pop_back();

    return result;
}

std::vector<RenamePreview> BatchRenamer::previewRename(
    const std::vector<std::string>& paths,
    const RenameTemplate& tmpl,
    const std::string& target_dir) const
{
    std::vector<RenamePreview> previews;
    std::unordered_set<std::string> generated;
    int seq = 1;

    for (auto& path : paths) {
        RenamePreview pv;
        pv.original_path = path;
        pv.conflict      = false;

        Catalog::PhotoRecord dummy{};
        dummy.path = path;

        std::string stem = applyTemplate(tmpl, dummy, seq++);

        size_t dot = path.rfind('.');
        std::string ext = (dot != std::string::npos) ? path.substr(dot) : "";
        if (tmpl.extension_case == "lower") {
            for (auto& c : ext) c = static_cast<char>(std::tolower(c));
        } else if (tmpl.extension_case == "upper") {
            for (auto& c : ext) c = static_cast<char>(std::toupper(c));
        }

        std::string dir = target_dir;
        if (dir.empty()) {
            size_t sep = path.rfind('\\');
            if (sep == std::string::npos) sep = path.rfind('/');
            dir = (sep != std::string::npos) ? path.substr(0, sep) : ".";
        }

        pv.new_name = stem + ext;
        pv.new_path = dir + "\\" + pv.new_name;

        if (generated.count(pv.new_path)) {
            pv.conflict = true;
            pv.conflict_path = pv.new_path;
        }
        generated.insert(pv.new_path);
        previews.push_back(pv);
    }
    return previews;
}

RenameResult BatchRenamer::executeRename(const std::vector<RenamePreview>& previews,
                                          bool skip_conflicts)
{
    RenameResult result{0, 0, 0, {}};
    for (auto& pv : previews) {
        if (pv.conflict && skip_conflicts) {
            ++result.skip_count;
            continue;
        }
        if (pv.original_path == pv.new_path) { ++result.skip_count; continue; }

        std::wstring src = FileSystem::Utf8ToWide(pv.original_path);
        std::wstring dst = FileSystem::Utf8ToWide(pv.new_path);

        if (MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            ++result.success_count;
        } else {
            ++result.error_count;
            result.errors.emplace_back(pv.original_path,
                "Error " + std::to_string(GetLastError()));
        }
    }
    return result;
}

RenameResult BatchRenamer::renameFiles(const std::vector<std::string>& paths,
                                        const RenameTemplate& tmpl,
                                        const std::string& target_dir,
                                        bool dry_run)
{
    auto previews = previewRename(paths, tmpl, target_dir);
    if (dry_run) {
        RenameResult r{0, static_cast<int>(previews.size()), 0, {}};
        return r;
    }
    return executeRename(previews);
}

} // namespace PhotoManager::Rename