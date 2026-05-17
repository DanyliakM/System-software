#pragma once
#include <string>
#include <vector>
#include <deque>
#include <optional>
#include <functional>
#include <chrono>
#include <cstdint>

namespace PhotoManager::Log {

enum class OpType {
    COPY,
    MOVE,
    DELETE_OP,
    RENAME,
    IMPORT,
    HASH_COMPUTE,
    EXIF_EDIT,
    TAG_ADD,
    RATING_CHANGE,
    COLLECTION_CHANGE,
    CATALOG_ADD,
    CATALOG_REMOVE
};

struct LogEntry {
    uint64_t    id;
    OpType      type;
    std::string source_path;
    std::string dest_path;
    std::string detail;
    std::string timestamp;
    bool        undone;
    bool        undoable;
};

class OperationLog {
public:
    explicit OperationLog(const std::string& log_file_path, size_t max_entries = 5000);

    uint64_t   log(OpType type,
                    const std::string& src,
                    const std::string& dst = "",
                    const std::string& detail = "");

    bool       undo(uint64_t entry_id);
    bool       undoLast();

    std::vector<LogEntry> getAll() const;
    std::vector<LogEntry> getByType(OpType type) const;
    std::vector<LogEntry> getRecent(size_t n) const;
    std::optional<LogEntry> getById(uint64_t id) const;

    void       clear();
    void       save() const;
    void       load();

    size_t     count() const;
    std::string statistics() const;
    std::string opTypeName(OpType t) const;

    void setUndoHandler(OpType type,
                         std::function<bool(const LogEntry&)> handler);

private:
    std::string currentTimestamp() const;
    std::string serialize(const LogEntry& e) const;
    bool        deserialize(const std::string& line, LogEntry& e) const;

    std::string  log_path_;
    size_t       max_entries_;
    uint64_t     next_id_{1};
    std::deque<LogEntry> entries_;

    std::unordered_map<int, std::function<bool(const LogEntry&)>> undo_handlers_;
};

} // namespace PhotoManager::Log