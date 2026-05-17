#pragma once
#include "../filesystem/file_ops.hpp"
#include "../filesystem/ntfs_ads.hpp"
#include "../filesystem/folder_watcher.hpp"
#include "../filesystem/registry_settings.hpp"
#include "../filesystem/operation_log.hpp"
#include "../catalog/catalog.hpp"
#include "../hash/hasher.hpp"
#include "../filter/search_engine.hpp"
#include "../rename/batch_renamer.hpp"
#include "../import/importer.hpp"
#include "thumbnail_cache.hpp"
#include <string>
#include <memory>

namespace PhotoManager::UI {

struct AppState {
    std::string catalog_path;
    std::string hash_cache_path;
    std::string current_folder;
    std::string destination_folder;

    std::unique_ptr<Catalog::PhotoCatalog>       catalog;
    std::unique_ptr<Hash::Hasher>                hasher;
    std::unique_ptr<Filter::SearchEngine>        search;
    std::unique_ptr<Import::Importer>            importer;
    std::unique_ptr<Rename::BatchRenamer>        renamer;
    std::unique_ptr<Thumb::ThumbnailCache>       thumbs;
    std::unique_ptr<Watch::FolderWatcher>        watcher;
    std::unique_ptr<Log::OperationLog>           oplog;
    std::unique_ptr<Settings::RegistrySettings>  settings;
};

class ConsoleUI {
public:
    ConsoleUI();
    void run();

private:
    void initState();
    void mainMenu();

    void menuFileOps();
    void menuImport();
    void menuSearch();
    void menuRename();
    void menuHash();
    void menuExif();
    void menuCatalog();
    void menuNtfsAds();
    void menuWatcher();
    void menuOperationLog();
    void menuThumbnails();

    void actionCopyFiles();
    void actionMoveFiles();
    void actionDeleteFiles();
    void actionPreviewFile();
    void actionSetFolders();

    void actionImportFromFolder();
    void actionImportFromDrive();
    void actionAutoOrganize();

    void actionSearchByCamera();
    void actionSearchByDate();
    void actionSearchByKeyword();
    void actionFullTextSearch();
    void actionFindDuplicates();
    void actionFindByGps();

    void actionBatchRename();
    void actionPreviewRename();
    void actionRenameHelp();

    void actionComputeHash();
    void actionVerifyHash();
    void actionCompareFiles();
    void actionShowHashCache();

    void actionViewExif();
    void actionEditExif();
    void actionShowCameraInfo();

    void actionListCatalog();
    void actionSearchCatalog();
    void actionRemoveFromCatalog();
    void actionCatalogStats();
    void actionRatingsManager();

    void actionNtfsWriteHash();
    void actionNtfsReadAll();
    void actionNtfsWriteNote();
    void actionNtfsListStreams();
    void actionNtfsCopyStreams();

    void actionWatcherStart();
    void actionWatcherPoll();

    void actionLogShow();
    void actionLogUndo();
    void actionLogStats();

    void actionThumbGenerate();
    void actionThumbPreviewAscii();
    void actionThumbCacheInfo();

    void printHeader(const std::string& title) const;
    void printSeparator() const;
    void printPhotoRecord(const Catalog::PhotoRecord& r, bool detailed) const;
    void printFileInfo(const FileSystem::FileInfo& fi) const;
    void printExifData(const Exif::ExifData& exif) const;
    void printImportProgress(const Import::ImportProgress& p) const;

    std::string promptLine(const std::string& prompt) const;
    int         promptInt(const std::string& prompt, int min, int max) const;
    bool        promptYesNo(const std::string& prompt) const;
    std::string promptFolder(const std::string& prompt) const;

    std::vector<FileSystem::FileInfo> scanCurrentFolder(bool recursive = false) const;
    std::vector<std::string>          selectFilesFromList(
        const std::vector<FileSystem::FileInfo>& files) const;

    void pauseForInput() const;
    void clearScreen() const;

    static std::string formatSize(uint64_t bytes);
    static std::string formatDate(const std::tm& tm);

    AppState state_;
};

} // namespace PhotoManager::UI