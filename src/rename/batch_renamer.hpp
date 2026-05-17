#pragma once
#include "../catalog/catalog.hpp"
#include <string>
#include <vector>
#include <functional>

namespace PhotoManager::Rename {

struct RenameTemplate {
    std::string pattern;
    std::string extension_case;

    static RenameTemplate byDate();
    static RenameTemplate byDateAndCamera();
    static RenameTemplate bySequence(const std::string& prefix);
    static RenameTemplate byEvent(const std::string& event);
    static RenameTemplate fromString(const std::string& pattern);
};

struct RenamePreview {
    std::string original_path;
    std::string new_name;
    std::string new_path;
    bool        conflict;
    std::string conflict_path;
};

struct RenameResult {
    int  success_count;
    int  skip_count;
    int  error_count;
    std::vector<std::pair<std::string,std::string>> errors;
};

class BatchRenamer {
public:
    BatchRenamer() = default;

    std::string applyTemplate(const RenameTemplate& tmpl,
                               const Catalog::PhotoRecord& rec,
                               int sequence_number) const;

    std::vector<RenamePreview> previewRename(
        const std::vector<std::string>& paths,
        const RenameTemplate& tmpl,
        const std::string& target_dir = "") const;

    RenameResult executeRename(const std::vector<RenamePreview>& previews,
                                bool skip_conflicts = true);

    RenameResult renameFiles(const std::vector<std::string>& paths,
                              const RenameTemplate& tmpl,
                              const std::string& target_dir = "",
                              bool dry_run = false);

    static std::vector<std::string> availableTokens();
    static void printTemplateHelp();

private:
    std::string expandToken(const std::string& token,
                             const Catalog::PhotoRecord& rec,
                             int seq) const;
    std::string padded(int n, int width) const;
    std::string safeNamePart(const std::string& s) const;
};

} // namespace PhotoManager::Rename