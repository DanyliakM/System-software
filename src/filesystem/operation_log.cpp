#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "operation_log.hpp"
#include "file_ops.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <cstdint>

namespace PhotoManager::Log {

OperationLog::OperationLog(const std::string& log_file_path, size_t max_entries)
    : log_path_(log_file_path), max_entries_(max_entries)
{
    load();

    setUndoHandler(OpType::COPY, [](const LogEntry& e) {
        if (e.dest_path.empty()) return false;
        return DeleteFileW(FileSystem::Utf8ToWide(e.dest_path).c_str()) != 0;
    });

    setUndoHandler(OpType::MOVE, [](const LogEntry& e) {
        if (e.source_path.empty() || e.dest_path.empty()) return false;
        return MoveFileExW(FileSystem::Utf8ToWide(e.dest_path).c_str(),
                            FileSystem::Utf8ToWide(e.source_path).c_str(),
                            MOVEFILE_COPY_ALLOWED) != 0;
    });

    setUndoHandler(OpType::RENAME, [](const LogEntry& e) {
        if (e.source_path.empty() || e.dest_path.empty()) return false;
        return MoveFileExW(FileSystem::Utf8ToWide(e.dest_path).c_str(),
                            FileSystem::Utf8ToWide(e.source_path).c_str(), 0) != 0;
    });
}

std::string OperationLog::currentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string OperationLog::opTypeName(OpType t) const {
    switch (t) {
    case OpType::COPY:              return "COPY";
    case OpType::MOVE:              return "MOVE";
    case OpType::DELETE_OP:         return "DELETE";
    case OpType::RENAME:            return "RENAME";
    case OpType::IMPORT:            return "IMPORT";
    case OpType::HASH_COMPUTE:      return "HASH";
    case OpType::EXIF_EDIT:         return "EXIF_EDIT";
    case OpType::TAG_ADD:           return "TAG_ADD";
    case OpType::RATING_CHANGE:     return "RATING";
    case OpType::COLLECTION_CHANGE: return "COLLECTION";
    case OpType::CATALOG_ADD:       return "CAT_ADD";
    case OpType::CATALOG_REMOVE:    return "CAT_DEL";
    default:                         return "UNKNOWN";
    }
}

static OpType opTypeFromName(const std::string& name) {
    if (name == "COPY")       return OpType::COPY;
    if (name == "MOVE")       return OpType::MOVE;
    if (name == "DELETE")     return OpType::DELETE_OP;
    if (name == "RENAME")     return OpType::RENAME;
    if (name == "IMPORT")     return OpType::IMPORT;
    if (name == "HASH")       return OpType::HASH_COMPUTE;
    if (name == "EXIF_EDIT")  return OpType::EXIF_EDIT;
    if (name == "TAG_ADD")    return OpType::TAG_ADD;
    if (name == "RATING")     return OpType::RATING_CHANGE;
    if (name == "COLLECTION") return OpType::COLLECTION_CHANGE;
    if (name == "CAT_ADD")    return OpType::CATALOG_ADD;
    if (name == "CAT_DEL")    return OpType::CATALOG_REMOVE;
    return OpType::COPY;
}

std::string OperationLog::serialize(const LogEntry& e) const {
    auto esc = [](const std::string& s) {
        std::string r;
        for (char c : s) {
            if (c=='|') r += "\\|";
            else if (c=='\\') r += "\\\\";
            else r += c;
        }
        return r;
    };
    return std::to_string(e.id) + "|" +
           opTypeName(e.type)   + "|" +
           esc(e.source_path)   + "|" +
           esc(e.dest_path)     + "|" +
           esc(e.detail)        + "|" +
           e.timestamp          + "|" +
           (e.undone    ? "1" : "0") + "|" +
           (e.undoable  ? "1" : "0");
}

bool OperationLog::deserialize(const std::string& line, LogEntry& e) const {
    std::vector<std::string> parts;
    std::string cur;
    bool esc = false;
    for (char c : line) {
        if (esc) {
            if (c=='|') cur += '|';
            else if (c=='\\') cur += '\\';
            else { cur += '\\'; cur += c; }
            esc = false;
        } else if (c=='\\') {
            esc = true;
        } else if (c=='|') {
            parts.push_back(cur); cur.clear();
        } else {
            cur += c;
        }
    }
    parts.push_back(cur);
    if (parts.size() < 8) return false;
    try {
        e.id          = std::stoull(parts[0]);
        e.type        = opTypeFromName(parts[1]);
        e.source_path = parts[2];
        e.dest_path   = parts[3];
        e.detail      = parts[4];
        e.timestamp   = parts[5];
        e.undone      = parts[6] == "1";
        e.undoable    = parts[7] == "1";
    } catch (...) { return false; }
    return true;
}

uint64_t OperationLog::log(OpType type,
                             const std::string& src,
                             const std::string& dst,
                             const std::string& detail)
{
    LogEntry e;
    e.id          = next_id_++;
    e.type        = type;
    e.source_path = src;
    e.dest_path   = dst;
    e.detail      = detail;
    e.timestamp   = currentTimestamp();
    e.undone      = false;
    e.undoable    = (type == OpType::COPY   ||
                     type == OpType::MOVE   ||
                     type == OpType::RENAME);

    entries_.push_back(e);
    if (entries_.size() > max_entries_) entries_.pop_front();
    save();
    return e.id;
}

bool OperationLog::undo(uint64_t entry_id) {
    for (auto& e : entries_) {
        if (e.id != entry_id) continue;
        if (e.undone || !e.undoable) return false;
        auto it = undo_handlers_.find(static_cast<int>(e.type));
        if (it == undo_handlers_.end()) return false;
        bool ok = it->second(e);
        if (ok) { e.undone = true; save(); }
        return ok;
    }
    return false;
}

bool OperationLog::undoLast() {
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->undoable && !it->undone)
            return undo(it->id);
    }
    return false;
}

std::vector<LogEntry> OperationLog::getAll() const {
    return {entries_.begin(), entries_.end()};
}

std::vector<LogEntry> OperationLog::getByType(OpType type) const {
    std::vector<LogEntry> result;
    for (auto& e : entries_) if (e.type == type) result.push_back(e);
    return result;
}

std::vector<LogEntry> OperationLog::getRecent(size_t n) const {
    std::vector<LogEntry> result;
    size_t start = entries_.size() > n ? entries_.size() - n : 0;
    for (size_t i = start; i < entries_.size(); ++i)
        result.push_back(entries_[i]);
    return result;
}

std::optional<LogEntry> OperationLog::getById(uint64_t id) const {
    for (auto& e : entries_) if (e.id == id) return e;
    return std::nullopt;
}

void OperationLog::clear() {
    entries_.clear();
    next_id_ = 1;
    save();
}

void OperationLog::save() const {
    std::ofstream f(log_path_, std::ios::trunc);
    if (!f.is_open()) return;
    f << "# PhotoManager Operation Log v1\n";
    for (auto& e : entries_) f << serialize(e) << '\n';
}

void OperationLog::load() {
    std::ifstream f(log_path_);
    if (!f.is_open()) return;
    entries_.clear();
    next_id_ = 1;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0]=='#') continue;
        LogEntry e{};
        if (deserialize(line, e)) {
            entries_.push_back(e);
            if (e.id >= next_id_) next_id_ = e.id + 1;
        }
    }
}

size_t OperationLog::count() const { return entries_.size(); }

std::string OperationLog::statistics() const {
    std::unordered_map<std::string, int> counts;
    int undone = 0;
    for (auto& e : entries_) {
        counts[opTypeName(e.type)]++;
        if (e.undone) ++undone;
    }
    std::ostringstream ss;
    ss << "Total operations: " << entries_.size() << " (undone: " << undone << ")\n";
    for (auto& [k,v] : counts) ss << "  " << k << ": " << v << "\n";
    return ss.str();
}

void OperationLog::setUndoHandler(OpType type,
                                    std::function<bool(const LogEntry&)> handler)
{
    undo_handlers_[static_cast<int>(type)] = std::move(handler);
}

} // namespace PhotoManager::Log