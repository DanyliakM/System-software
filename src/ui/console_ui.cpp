#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "console_ui.hpp"
#include "../../src/exif/exif_parser.hpp"
#include "src/filesystem/ntfs_ads.hpp"
#include "src/ui/thumbnail_cache.hpp"
#include "src/hash/hasher.hpp"
#include "src/filesystem/file_ops.hpp"
#include <shlobj.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <fstream>

namespace fs = std::filesystem;
namespace PhotoManager::UI {

ConsoleUI::ConsoleUI() { initState(); }

void ConsoleUI::initState() {
    wchar_t appdata[MAX_PATH];
    std::string base;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        base = FileSystem::WideToUtf8(appdata) + "\\PhotoManager";
        CreateDirectoryW(FileSystem::Utf8ToWide(base).c_str(), nullptr);
    } else {
        base = ".";
    }

    state_.catalog_path    = base + "\\catalog.pmdb";
    state_.hash_cache_path = base + "\\hashcache.pmc";

    state_.settings = std::make_unique<Settings::RegistrySettings>();
    state_.settings->open();

    auto last = state_.settings->getLastOpenedFolder();
    if (!last.empty()) state_.current_folder = FileSystem::WideToUtf8(last);

    state_.catalog  = std::make_unique<Catalog::PhotoCatalog>(state_.catalog_path);
    state_.catalog->load();
    state_.hasher   = std::make_unique<Hash::Hasher>(state_.hash_cache_path);
    state_.search   = std::make_unique<Filter::SearchEngine>(*state_.catalog);
    state_.importer = std::make_unique<Import::Importer>(*state_.catalog, *state_.hasher);
    state_.renamer  = std::make_unique<Rename::BatchRenamer>();
    state_.thumbs   = std::make_unique<Thumb::ThumbnailCache>(
                          FileSystem::Utf8ToWide(base + "\\thumbcache"), 256);
    state_.watcher  = std::make_unique<Watch::FolderWatcher>();
    state_.oplog    = std::make_unique<Log::OperationLog>(base + "\\oplog.pmlog");
}

void ConsoleUI::clearScreen() const {
    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hCon, &csbi);
    DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD written;
    COORD origin{0, 0};
    FillConsoleOutputCharacterW(hCon, L' ', cells, origin, &written);
    FillConsoleOutputAttribute(hCon, csbi.wAttributes, cells, origin, &written);
    SetConsoleCursorPosition(hCon, origin);
}

void ConsoleUI::printSeparator() const {
    std::cout << std::string(60, '=') << '\n';
}

void ConsoleUI::printHeader(const std::string& title) const {
    std::cout << '\n';
    printSeparator();
    std::cout << "  " << title << '\n';
    printSeparator();
}

void ConsoleUI::pauseForInput() const {
    std::cout << "\n  [Press ENTER to continue...] ";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

std::string ConsoleUI::promptLine(const std::string& prompt) const {
    std::cout << "  " << prompt;
    std::string line;
    std::getline(std::cin, line);
    return line;
}

int ConsoleUI::promptInt(const std::string& prompt, int lo, int hi) const {
    while (true) {
        std::string s = promptLine(prompt);
        try {
            int v = std::stoi(s);
            if (v >= lo && v <= hi) return v;
        } catch (...) {}
        std::cout << "  Invalid input. Enter a number between " << lo << " and " << hi << '\n';
    }
}

bool ConsoleUI::promptYesNo(const std::string& prompt) const {
    std::string s = promptLine(prompt + " [y/n]: ");
    return !s.empty() && (s[0]=='y' || s[0]=='Y');
}

std::string ConsoleUI::promptFolder(const std::string& prompt) const {
    return promptLine(prompt + " (full path): ");
}

std::string ConsoleUI::formatSize(uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024*1024) return std::to_string(bytes/1024) + " KB";
    if (bytes < 1024*1024*1024) return std::to_string(bytes/(1024*1024)) + " MB";
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << bytes/1073741824.0 << " GB";
    return ss.str();
}

std::string ConsoleUI::formatDate(const std::tm& tm) {
    std::ostringstream ss;
    ss << std::setw(4) << std::setfill('0') << (tm.tm_year+1900) << '-'
       << std::setw(2) << std::setfill('0') << (tm.tm_mon+1) << '-'
       << std::setw(2) << std::setfill('0') << tm.tm_mday << ' '
       << std::setw(2) << std::setfill('0') << tm.tm_hour << ':'
       << std::setw(2) << std::setfill('0') << tm.tm_min  << ':'
       << std::setw(2) << std::setfill('0') << tm.tm_sec;
    return ss.str();
}

void ConsoleUI::printFileInfo(const FileSystem::FileInfo& fi) const {
    std::cout << "  Name     : " << FileSystem::WideToUtf8(fi.name) << '\n'
              << "  Extension: " << FileSystem::WideToUtf8(fi.extension) << '\n'
              << "  Size     : " << formatSize(fi.size_bytes) << '\n'
              << "  Modified : " << formatDate(fi.date_modified) << '\n'
              << "  Created  : " << formatDate(fi.date_created) << '\n'
              << "  Path     : " << FileSystem::WideToUtf8(fi.full_path) << '\n';
}

void ConsoleUI::printExifData(const Exif::ExifData& exif) const {
    auto p = [](const std::string& k, const std::string& v){
        if (!v.empty()) std::cout << "  " << std::left << std::setw(18) << k << ": " << v << '\n';
    };
    p("Camera Make",    exif.camera_make);
    p("Camera Model",   exif.camera_model);
    p("Lens Make",      exif.lens_make);
    p("Lens Model",     exif.lens_model);
    p("Software",       exif.software);
    p("Date Taken",     Exif::ExifParser::formatDateTime(exif.datetime_original));
    p("Artist",         exif.artist);
    p("Copyright",      exif.copyright);
    p("Description",    exif.image_description);
    p("User Comment",   exif.user_comment);

    if (exif.image_width && exif.image_height)
        std::cout << "  " << std::left << std::setw(18) << "Resolution"
                  << ": " << exif.image_width << " x " << exif.image_height << '\n';
    if (exif.focal_length > 0)
        std::cout << "  " << std::left << std::setw(18) << "Focal Length"
                  << ": " << std::fixed << std::setprecision(1) << exif.focal_length << " mm\n";
    if (exif.f_number > 0)
        std::cout << "  " << std::left << std::setw(18) << "Aperture"
                  << ": " << Exif::ExifParser::fNumberStr(exif.f_number) << '\n';
    if (exif.exposure_time > 0)
        std::cout << "  " << std::left << std::setw(18) << "Shutter Speed"
                  << ": " << Exif::ExifParser::exposureTimeStr(exif.exposure_time) << '\n';
    if (exif.iso_speed > 0)
        std::cout << "  " << std::left << std::setw(18) << "ISO"
                  << ": " << exif.iso_speed << '\n';
    if (exif.has_gps && exif.gps_latitude && exif.gps_longitude) {
        std::cout << "  " << std::left << std::setw(18) << "GPS"
                  << ": " << std::fixed << std::setprecision(6)
                  << exif.gps_latitude->toDecimal() << ", "
                  << exif.gps_longitude->toDecimal() << '\n';
    }
}

void ConsoleUI::printPhotoRecord(const Catalog::PhotoRecord& r, bool detailed) const {
    std::cout << "  [" << std::setw(4) << r.id << "] " << r.path << '\n';
    if (detailed) {
        if (!r.camera_model.empty())
            std::cout << "         Camera   : " << r.camera_make << " " << r.camera_model << '\n';
        if (!r.lens_model.empty())
            std::cout << "         Lens     : " << r.lens_model << '\n';
        if (!r.date_taken.empty())
            std::cout << "         Date     : " << r.date_taken << '\n';
        std::cout << "         Size     : " << formatSize(r.file_size)
                  << "  (" << r.image_width << "x" << r.image_height << ")\n";
        if (r.rating > 0)
            std::cout << "         Rating   : " << std::string(r.rating, '*') << '\n';
        if (!r.keywords.empty())
            std::cout << "         Keywords : " << r.keywords << '\n';
        if (r.has_gps)
            std::cout << "         GPS      : " << std::fixed << std::setprecision(5)
                      << r.gps_lat << ", " << r.gps_lon << '\n';
    }
}

std::vector<FileSystem::FileInfo> ConsoleUI::scanCurrentFolder(bool recursive) const {
    if (state_.current_folder.empty()) return {};
    std::wstring w = FileSystem::Utf8ToWide(state_.current_folder);
    return FileSystem::ScanDirectory(w, recursive, true);
}

std::vector<std::string> ConsoleUI::selectFilesFromList(
    const std::vector<FileSystem::FileInfo>& files) const
{
    std::cout << '\n';
    for (size_t i = 0; i < files.size(); ++i) {
        std::cout << "  [" << std::setw(3) << (i+1) << "] "
                  << FileSystem::WideToUtf8(files[i].name)
                  << " (" << formatSize(files[i].size_bytes) << ")\n";
    }
    std::cout << "  [  0] All files\n";
    std::string sel = promptLine("Select (comma-separated or 0): ");

    std::vector<std::string> result;
    if (sel == "0") {
        for (auto& f : files) result.push_back(FileSystem::WideToUtf8(f.full_path));
        return result;
    }
    std::istringstream ss(sel);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        try {
            int idx = std::stoi(tok) - 1;
            if (idx >= 0 && idx < static_cast<int>(files.size()))
                result.push_back(FileSystem::WideToUtf8(files[idx].full_path));
        } catch (...) {}
    }
    return result;
}

void ConsoleUI::run() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hCon, &mode);
    SetConsoleMode(hCon, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    mainMenu();
}

void ConsoleUI::mainMenu() {
    while (true) {
        clearScreen();
        std::cout << R"(
  ____  _           _        __  __
 |  _ \| |__   ___ | |_ ___ |  \/  | __ _ _ __   __ _  __ _  ___ _ __
 | |_) | '_ \ / _ \| __/ _ \| |\/| |/ _` | '_ \ / _` |/ _` |/ _ \ '__|
 |  __/| | | | (_) | || (_) | |  | | (_| | | | | (_| | (_| |  __/ |
 |_|   |_| |_|\___/ \__\___/|_|  |_|\__,_|_| |_|\__,_|\__, |\___|_|
                                                         |___/
)";
        printSeparator();
        std::cout << "  Catalog: " << state_.catalog->count() << " photos"
                  << "   Source: " << (state_.current_folder.empty() ? "[not set]" : state_.current_folder)
                  << "\n  Dest: " << (state_.destination_folder.empty() ? "[not set]" : state_.destination_folder) << '\n';
        printSeparator();
        std::cout << "\n  1. File Operations   (copy/move/delete/preview)\n"
                  << "  2. Import Photos     (from folder or drive)\n"
                  << "  3. Search & Filter   (by camera/date/keyword/GPS)\n"
                  << "  4. Batch Rename      (template-based renaming)\n"
                  << "  5. Hash & Integrity  (compute/verify/compare)\n"
                  << "  6. EXIF Metadata     (view/edit EXIF/IPTC)\n"
                  << "  7. Catalog Manager   (browse/rate/tag photos)\n"
                  << "  8. NTFS Streams      (embed metadata in ADS)\n"
                  << "  9. Folder Watcher    (monitor folder live)\n"
                  << " 10. Operation Log     (history + undo)\n"
                  << " 11. Thumbnails        (generate/preview/ASCII)\n"
                  << "  0. Exit\n\n";

        int ch = promptInt("Choose: ", 0, 11);
        switch (ch) {
        case 1: menuFileOps();     break;
        case 2: menuImport();      break;
        case 3: menuSearch();      break;
        case 4: menuRename();      break;
        case 5: menuHash();        break;
        case 6: menuExif();        break;
        case 7: menuCatalog();     break;
        case 8: menuNtfsAds();     break;
        case 9: menuWatcher();     break;
        case 10: menuOperationLog(); break;
        case 11: menuThumbnails(); break;
        case 0:
            state_.catalog->save();
            state_.hasher->saveCache();
            if (!state_.current_folder.empty())
                state_.settings->saveLastOpenedFolder(
                    FileSystem::Utf8ToWide(state_.current_folder));
            state_.settings->addRecentFolder(
                FileSystem::Utf8ToWide(state_.current_folder));
            if (state_.watcher->isRunning()) state_.watcher->stopWatching();
            std::cout << "\n  Catalog saved. Settings saved. Goodbye!\n";
            return;
        }
    }
}

void ConsoleUI::menuFileOps() {
    while (true) {
        printHeader("FILE OPERATIONS");
        std::cout << "  1. Set source / destination folders\n"
                  << "  2. Copy photos\n"
                  << "  3. Move photos\n"
                  << "  4. Delete photos\n"
                  << "  5. Preview / open file\n"
                  << "  6. Reveal in Explorer\n"
                  << "  7. List drives\n"
                  << "  0. Back\n\n";

        int ch = promptInt("Choose: ", 0, 7);
        if (ch == 0) break;
        switch (ch) {
        case 1: actionSetFolders(); break;
        case 2: actionCopyFiles();  break;
        case 3: actionMoveFiles();  break;
        case 4: actionDeleteFiles();break;
        case 5: actionPreviewFile();break;
        case 6: {
            std::string p = promptLine("Path to reveal: ");
            if (!FileSystem::RevealInExplorer(FileSystem::Utf8ToWide(p)))
                std::cout << "  Failed to reveal in Explorer.\n";
            break;
        }
        case 7: {
            auto drives = FileSystem::EnumerateDrives();
            printHeader("DRIVES");
            for (auto& d : drives) {
                std::cout << "  " << FileSystem::WideToUtf8(d.letter)
                          << " [" << FileSystem::WideToUtf8(d.label) << "] "
                          << FileSystem::WideToUtf8(d.filesystem)
                          << " Total:" << formatSize(d.total_bytes)
                          << " Free:" << formatSize(d.free_bytes);
                if (d.drive_type == DRIVE_REMOVABLE) std::cout << " [REMOVABLE]";
                std::cout << '\n';
            }
            pauseForInput();
            break;
        }
        }
    }
}

void ConsoleUI::actionSetFolders() {
    printHeader("SET FOLDERS");
    std::string src = promptFolder("Source folder");
    if (!src.empty()) state_.current_folder = src;
    std::string dst = promptFolder("Destination folder");
    if (!dst.empty()) state_.destination_folder = dst;
    std::cout << "  Folders updated.\n";
    pauseForInput();
}

void ConsoleUI::actionCopyFiles() {
    if (state_.current_folder.empty() || state_.destination_folder.empty()) {
        std::cout << "  Please set source and destination folders first.\n";
        pauseForInput(); return;
    }
    auto files = scanCurrentFolder();
    if (files.empty()) { std::cout << "  No photos found.\n"; pauseForInput(); return; }
    auto sel = selectFilesFromList(files);
    if (sel.empty()) return;
    bool overwrite = promptYesNo("Overwrite existing?");
    int ok=0, fail=0;
    for (auto& p : sel) {
        std::wstring src = FileSystem::Utf8ToWide(p);
        std::wstring dst = FileSystem::Utf8ToWide(state_.destination_folder);
        if (FileSystem::CopyPhoto(src, dst, overwrite)) { ++ok;
            auto dst_path = dst + L"\\" + src.substr(src.rfind(L'\\')+1);
            auto exif = Exif::ExifParser::parse(src);
            Exif::ExifData ed; if(exif) ed=*exif;
            uint64_t sz = Hash::Hasher::getFileSize(src);            
            auto rec = Catalog::PhotoCatalog::fromExif(
                FileSystem::WideToUtf8(dst_path), sz, "", ed);
            state_.catalog->addPhoto(rec);
            state_.oplog->log(Log::OpType::COPY, p,
                FileSystem::WideToUtf8(dst_path));
        } else ++fail;
    }
    std::cout << "  Copied: " << ok << "  Failed: " << fail << '\n';
    state_.catalog->save();
    pauseForInput();
}

void ConsoleUI::actionMoveFiles() {
    if (state_.current_folder.empty() || state_.destination_folder.empty()) {
        std::cout << "  Please set source and destination folders first.\n";
        pauseForInput(); return;
    }
    auto files = scanCurrentFolder();
    if (files.empty()) { std::cout << "  No photos found.\n"; pauseForInput(); return; }
    auto sel = selectFilesFromList(files);
    if (sel.empty()) return;
    int ok=0, fail=0;
    for (auto& p : sel) {
        std::wstring src = FileSystem::Utf8ToWide(p);
        std::wstring dst = FileSystem::Utf8ToWide(state_.destination_folder);
        if (FileSystem::MovePhoto(src, dst)) ++ok;
        else ++fail;
    }
    std::cout << "  Moved: " << ok << "  Failed: " << fail << '\n';
    pauseForInput();
}

void ConsoleUI::actionDeleteFiles() {
    auto files = scanCurrentFolder();
    if (files.empty()) { std::cout << "  No photos found.\n"; pauseForInput(); return; }
    auto sel = selectFilesFromList(files);
    if (sel.empty()) return;
    bool recycle = promptYesNo("Send to Recycle Bin (vs permanent delete)?");
    if (!promptYesNo("Are you sure?")) return;
    int ok=0, fail=0;
    for (auto& p : sel) {
        if (FileSystem::DeletePhoto(FileSystem::Utf8ToWide(p), recycle)) {
            state_.catalog->removeByPath(p); ++ok;
        } else ++fail;
    }
    std::cout << "  Deleted: " << ok << "  Failed: " << fail << '\n';
    state_.catalog->save();
    pauseForInput();
}

void ConsoleUI::actionPreviewFile() {
    auto files = scanCurrentFolder();
    if (files.empty()) { std::cout << "  No photos found.\n"; pauseForInput(); return; }
    auto sel = selectFilesFromList(files);
    for (auto& p : sel)
        FileSystem::OpenWithDefaultApp(FileSystem::Utf8ToWide(p));
}

void ConsoleUI::menuImport() {
    while (true) {
        printHeader("IMPORT PHOTOS");
        std::cout << "  1. Import from folder\n"
                  << "  2. Import from removable drive\n"
                  << "  3. Auto-organize existing folder\n"
                  << "  0. Back\n\n";
        int ch = promptInt("Choose: ", 0, 3);
        if (ch == 0) break;
        switch (ch) {
        case 1: actionImportFromFolder(); break;
        case 2: actionImportFromDrive();  break;
        case 3: actionAutoOrganize();     break;
        }
    }
}

void ConsoleUI::actionImportFromFolder() {
    printHeader("IMPORT FROM FOLDER");
    Import::ImportOptions opts;
    opts.source_path      = promptFolder("Source folder");
    opts.destination_path = promptFolder("Destination folder");
    if (opts.source_path.empty() || opts.destination_path.empty()) return;
    opts.recursive        = promptYesNo("Scan sub-folders?");
    opts.delete_source    = promptYesNo("Move files (delete from source)?");
    opts.skip_duplicates  = promptYesNo("Skip duplicates?");
    opts.compute_hash     = promptYesNo("Compute SHA-256 hashes?");
    opts.collection_name  = promptLine("Collection name (optional): ");
    opts.event_name       = promptLine("Event name (optional): ");

    std::cout << "\n  Organize mode:\n"
              << "  1. YYYY\\YYYY-MM\n"
              << "  2. YYYY\\YYYY-MM\\YYYY-MM-DD\n"
              << "  3. YYYY-MM-DD (flat)\n"
              << "  4. By camera\n"
              << "  5. By camera + date\n"
              << "  6. Flat (no subfolders)\n";
    int mode = promptInt("Mode: ", 1, 6);
    opts.organize_mode = static_cast<Import::OrganizeMode>(mode - 1);

    std::cout << "\n  Starting import...\n\n";
    auto result = state_.importer->importFromPath(opts,
        [this](const Import::ImportProgress& p){ printImportProgress(p); });

    printSeparator();
    std::cout << "  Total found : " << result.total_found << '\n'
              << "  Imported    : " << result.imported << '\n'
              << "  Duplicates  : " << result.duplicates_skipped << '\n'
              << "  Errors      : " << result.errors << '\n';
    state_.catalog->save();
    pauseForInput();
}

void ConsoleUI::actionImportFromDrive() {
    auto removable = state_.importer->getRemovableDrives();
    if (removable.empty()) {
        std::cout << "  No removable drives found.\n";
        pauseForInput(); return;
    }
    printHeader("SELECT DRIVE");
    for (size_t i=0; i<removable.size(); ++i) {
        std::cout << "  [" << (i+1) << "] "
                  << FileSystem::WideToUtf8(removable[i].letter)
                  << " " << FileSystem::WideToUtf8(removable[i].label)
                  << " (" << formatSize(removable[i].total_bytes) << ")\n";
    }
    int idx = promptInt("Select drive: ", 1, static_cast<int>(removable.size())) - 1;
    Import::ImportOptions opts;
    opts.destination_path = promptFolder("Destination folder");
    if (opts.destination_path.empty()) return;
    opts.recursive       = true;
    opts.skip_duplicates = true;
    opts.compute_hash    = true;
    opts.organize_mode   = Import::OrganizeMode::BY_YEAR_MONTH;
    opts.collection_name = promptLine("Collection name (optional): ");

    auto result = state_.importer->importFromDrive(removable[idx].letter, opts,
        [this](const Import::ImportProgress& p){ printImportProgress(p); });

    std::cout << "\n  Imported: " << result.imported
              << "  Skipped: " << result.duplicates_skipped
              << "  Errors: " << result.errors << '\n';
    state_.catalog->save();
    pauseForInput();
}

void ConsoleUI::actionAutoOrganize() {
    std::cout << "  Auto-organize moves photos from a source folder\n"
              << "  into structured sub-folders based on EXIF date.\n\n";
    actionImportFromFolder();
}

void ConsoleUI::printImportProgress(const Import::ImportProgress& p) const {
    std::cout << "\r  [" << std::setw(5) << p.processed << "/" << p.total << "] "
              << std::setw(4) << (p.total ? p.processed*100/p.total : 0) << "% "
              << p.current_file.substr(0, 40) << std::string(42, ' ') << std::flush;
}

void ConsoleUI::menuSearch() {
    while (true) {
        printHeader("SEARCH & FILTER");
        std::cout << "  1. Full-text search\n"
                  << "  2. Search by camera\n"
                  << "  3. Search by lens\n"
                  << "  4. Search by date range\n"
                  << "  5. Search by keyword/tag\n"
                  << "  6. Search by GPS radius\n"
                  << "  7. Find duplicates\n"
                  << "  8. Advanced filter\n"
                  << "  0. Back\n\n";
        int ch = promptInt("Choose: ", 0, 8);
        if (ch == 0) break;
        switch (ch) {
        case 1: actionFullTextSearch();  break;
        case 2: actionSearchByCamera();  break;
        case 3: {
            auto lenses = state_.catalog->getDistinctLenses();
            if (!lenses.empty()) {
                std::cout << "\n  Known lenses:\n";
                for (auto& l : lenses) std::cout << "    " << l << '\n';
            }
            std::string lens = promptLine("Lens model: ");
            auto res = state_.search->findByLens(lens);
            std::cout << "\n  Found " << res.total_count << " photos:\n";
            for (auto& r : res.photos) printPhotoRecord(r, true);
            pauseForInput();
            break;
        }
        case 4: actionSearchByDate();    break;
        case 5: actionSearchByKeyword(); break;
        case 6: actionFindByGps();       break;
        case 7: actionFindDuplicates();  break;
        case 8: actionSearchCatalog();   break;
        }
    }
}

void ConsoleUI::actionFullTextSearch() {
    std::string q = promptLine("Search term: ");
    auto res = state_.search->fullTextSearch(q);
    std::cout << "\n  Found " << res.total_count << " photos in "
              << std::fixed << std::setprecision(1) << res.elapsed_ms << " ms:\n";
    for (auto& r : res.photos) printPhotoRecord(r, false);
    pauseForInput();
}

void ConsoleUI::actionSearchByCamera() {
    auto cams = state_.catalog->getDistinctCameras();
    if (!cams.empty()) {
        std::cout << "\n  Known cameras:\n";
        for (size_t i=0; i<cams.size(); ++i)
            std::cout << "  [" << (i+1) << "] " << cams[i] << '\n';
    }
    std::string cam = promptLine("Camera name (or partial): ");
    auto res = state_.search->findByCamera(cam);
    std::cout << "\n  Found " << res.total_count << " photos:\n";
    for (auto& r : res.photos) printPhotoRecord(r, true);
    pauseForInput();
}

void ConsoleUI::actionSearchByDate() {
    std::string from = promptLine("From date (YYYY-MM-DD): ");
    std::string to   = promptLine("To date   (YYYY-MM-DD): ");
    if (to.empty()) to = "9999-12-31";
    auto res = state_.search->findByDateRange(from, to);
    std::cout << "\n  Found " << res.total_count << " photos:\n";
    for (auto& r : res.photos) printPhotoRecord(r, false);
    pauseForInput();
}

void ConsoleUI::actionSearchByKeyword() {
    std::string kw = promptLine("Keyword/tag: ");
    auto res = state_.search->findByKeyword(kw);
    std::cout << "\n  Found " << res.total_count << " photos:\n";
    for (auto& r : res.photos) printPhotoRecord(r, true);
    pauseForInput();
}

void ConsoleUI::actionFindByGps() {
    std::string lat_s = promptLine("Latitude: ");
    std::string lon_s = promptLine("Longitude: ");
    std::string rad_s = promptLine("Radius (km): ");
    try {
        double lat = std::stod(lat_s);
        double lon = std::stod(lon_s);
        double rad = std::stod(rad_s);
        auto res = state_.search->findByGpsRadius(lat, lon, rad);
        std::cout << "\n  Found " << res.total_count << " photos within "
                  << rad << " km:\n";
        for (auto& r : res.photos) printPhotoRecord(r, true);
    } catch (...) {
        std::cout << "  Invalid coordinates.\n";
    }
    pauseForInput();
}

void ConsoleUI::actionFindDuplicates() {
    auto res = state_.search->findDuplicates();
    std::cout << "\n  Found " << res.total_count << " duplicate photos:\n";
    for (auto& r : res.photos) printPhotoRecord(r, false);
    if (!res.photos.empty() && promptYesNo("Open duplicates folder for review?")) {
        if (!res.photos.empty())
            FileSystem::RevealInExplorer(FileSystem::Utf8ToWide(res.photos[0].path));
    }
    pauseForInput();
}

void ConsoleUI::menuRename() {
    while (true) {
        printHeader("BATCH RENAME");
        std::cout << "  1. Rename with date template\n"
                  << "  2. Rename with camera+date template\n"
                  << "  3. Rename with event name\n"
                  << "  4. Custom template rename\n"
                  << "  5. Preview rename (dry run)\n"
                  << "  6. Template help\n"
                  << "  0. Back\n\n";
        int ch = promptInt("Choose: ", 0, 6);
        if (ch == 0) break;
        switch (ch) {
        case 1: case 2: case 3: case 4: actionBatchRename(); break;
        case 5: actionPreviewRename(); break;
        case 6: actionRenameHelp(); break;
        }
    }
}

void ConsoleUI::actionBatchRename() {
    auto files = scanCurrentFolder();
    if (files.empty()) { std::cout << "  No photos found.\n"; pauseForInput(); return; }
    auto sel = selectFilesFromList(files);
    if (sel.empty()) return;

    std::cout << "\n  Template examples:\n"
              << "  {YYYY}{MM}{DD}_{HH}{mm}{ss}\n"
              << "  {YYYY}-{MM}-{DD}_{CAMERA}_{SEQ:4}\n"
              << "  vacation_{YYYY}{MM}{DD}_{SEQ:3}\n\n";
    std::string pattern = promptLine("Enter template: ");
    std::string event   = promptLine("Event name (optional): ");

    Rename::RenameTemplate tmpl;
    tmpl.pattern = pattern.empty() ? "{YYYY}{MM}{DD}_{SEQ:4}" : pattern;
    tmpl.extension_case = "lower";

    auto previews = state_.renamer->previewRename(sel, tmpl);
    std::cout << "\n  Preview:\n";
    for (auto& pv : previews) {
        size_t sep = pv.original_path.rfind('\\');
        std::string orig = (sep!=std::string::npos) ? pv.original_path.substr(sep+1) : pv.original_path;
        std::cout << "  " << orig << " -> " << pv.new_name;
        if (pv.conflict) std::cout << " [CONFLICT]";
        std::cout << '\n';
    }

    if (promptYesNo("\nApply rename?")) {
        auto result = state_.renamer->executeRename(previews);
        std::cout << "  Renamed: " << result.success_count
                  << "  Skipped: " << result.skip_count
                  << "  Errors: " << result.error_count << '\n';
    }
    pauseForInput();
}

void ConsoleUI::actionPreviewRename() {
    auto files = scanCurrentFolder();
    if (files.empty()) { std::cout << "  No photos.\n"; pauseForInput(); return; }
    std::vector<std::string> paths;
    for (auto& f : files) paths.push_back(FileSystem::WideToUtf8(f.full_path));
    std::string pattern = promptLine("Template: ");
    Rename::RenameTemplate tmpl = Rename::RenameTemplate::fromString(pattern);
    auto previews = state_.renamer->previewRename(paths, tmpl);
    for (auto& pv : previews)
        std::cout << "  " << pv.original_path << "\n    -> " << pv.new_path << '\n';
    pauseForInput();
}

void ConsoleUI::actionRenameHelp() {
    printHeader("RENAME TEMPLATE TOKENS");
    Rename::BatchRenamer::printTemplateHelp();
    pauseForInput();
}

void ConsoleUI::menuHash() {
    while (true) {
        printHeader("HASH & INTEGRITY");
        std::cout << "  1. Compute hash of a file\n"
                  << "  2. Verify file against stored hash\n"
                  << "  3. Compare two files\n"
                  << "  4. Hash all files in folder\n"
                  << "  5. Show hash cache info\n"
                  << "  0. Back\n\n";
        int ch = promptInt("Choose: ", 0, 5);
        if (ch == 0) break;
        switch (ch) {
        case 1: actionComputeHash();  break;
        case 2: actionVerifyHash();   break;
        case 3: actionCompareFiles(); break;
        case 4: {
            auto files = scanCurrentFolder(false);
            std::cout << "\n  Hashing " << files.size() << " files...\n";
            for (auto& fi : files) {
                try {
                    std::string h = state_.hasher->computeFileHash(fi.full_path);
                    std::cout << "  " << FileSystem::WideToUtf8(fi.name)
                              << "\n    SHA-256: " << h << '\n';
                } catch (const std::exception& e) {
                    std::cout << "  ERROR: " << e.what() << '\n';
                }
            }
            state_.hasher->saveCache();
            pauseForInput();
            break;
        }
        case 5: actionShowHashCache(); break;
        }
    }
}

void ConsoleUI::actionComputeHash() {
    std::string p = promptLine("File path: ");
    if (p.empty()) return;
    try {
        std::string h = state_.hasher->computeFileHash(FileSystem::Utf8ToWide(p));
        std::cout << "\n  File  : " << p << '\n'
                  << "  SHA-256: " << h << '\n';
        state_.hasher->saveCache();
    } catch (const std::exception& e) {
        std::cout << "  Error: " << e.what() << '\n';
    }
    pauseForInput();
}

void ConsoleUI::actionVerifyHash() {
    std::string p = promptLine("File path: ");
    std::string expected = promptLine("Expected SHA-256 hash: ");
    if (p.empty() || expected.empty()) return;
    bool ok = state_.hasher->verifyIntegrity(FileSystem::Utf8ToWide(p), expected);
    std::cout << "\n  Verification: " << (ok ? "PASSED - file is intact" : "FAILED - file may be corrupted") << '\n';
    pauseForInput();
}

void ConsoleUI::actionCompareFiles() {
    std::string a = promptLine("File 1 path: ");
    std::string b = promptLine("File 2 path: ");
    if (a.empty() || b.empty()) return;
    bool same = state_.hasher->filesAreIdentical(
        FileSystem::Utf8ToWide(a), FileSystem::Utf8ToWide(b));
    std::cout << "\n  Files are " << (same ? "IDENTICAL" : "DIFFERENT") << '\n';
    pauseForInput();
}

void ConsoleUI::actionShowHashCache() {
    std::cout << "\n  Hash cache: " << state_.hasher->cacheSize() << " entries\n"
              << "  Cache file: " << state_.hash_cache_path << '\n';
    pauseForInput();
}

void ConsoleUI::menuExif() {
    while (true) {
        printHeader("EXIF METADATA");
        std::cout << "  1. View EXIF of a file\n"
                  << "  2. View EXIF of files in folder\n"
                  << "  3. Edit EXIF (artist/copyright)\n"
                  << "  4. Show camera summary\n"
                  << "  0. Back\n\n";
        int ch = promptInt("Choose: ", 0, 4);
        if (ch == 0) break;
        switch (ch) {
        case 1: actionViewExif();     break;
        case 2: {
            auto files = scanCurrentFolder();
            for (auto& fi : files) {
                std::cout << "\n  --- " << FileSystem::WideToUtf8(fi.name) << " ---\n";
                auto exif = Exif::ExifParser::parse(fi.full_path);
                if (exif) printExifData(*exif);
                else std::cout << "  No EXIF data found.\n";
            }
            pauseForInput();
            break;
        }
        case 3: actionEditExif();     break;
        case 4: actionShowCameraInfo(); break;
        }
    }
}

void ConsoleUI::actionViewExif() {
    std::string p = promptLine("File path: ");
    if (p.empty()) return;
    auto exif = Exif::ExifParser::parse(FileSystem::Utf8ToWide(p));
    printHeader("EXIF DATA");
    if (exif) {
        printExifData(*exif);
        auto iptc = Exif::ExifParser::parseIptc(FileSystem::Utf8ToWide(p));
        if (iptc && !iptc->keywords.empty())
            std::cout << "  IPTC Keywords : " << iptc->keywords << '\n';
    } else {
        std::cout << "  No EXIF data found or not a JPEG file.\n";
    }
    pauseForInput();
}

void ConsoleUI::actionEditExif() {
    std::string p = promptLine("File path: ");
    if (p.empty()) return;
    std::cout << "  1. Set artist\n  2. Set copyright\n  3. Set user comment\n";
    int ch = promptInt("Choose: ", 1, 3);
    std::string val = promptLine("New value: ");
    bool ok = false;
    std::wstring wp = FileSystem::Utf8ToWide(p);
    switch (ch) {
    case 1: ok = Exif::ExifParser::writeArtist(wp, val); break;
    case 2: ok = Exif::ExifParser::writeCopyright(wp, val); break;
    case 3: ok = Exif::ExifParser::writeUserComment(wp, val); break;
    }
    std::cout << (ok ? "  Done." : "  Failed to write EXIF.") << '\n';
    pauseForInput();
}

void ConsoleUI::actionShowCameraInfo() {
    auto cams = state_.catalog->getDistinctCameras();
    auto lenses = state_.catalog->getDistinctLenses();
    printHeader("CAMERA & LENS SUMMARY");
    std::cout << "  Cameras:\n";
    for (auto& c : cams) std::cout << "    " << c << '\n';
    std::cout << "\n  Lenses:\n";
    for (auto& l : lenses) std::cout << "    " << l << '\n';
    pauseForInput();
}

void ConsoleUI::menuCatalog() {
    while (true) {
        printHeader("CATALOG MANAGER");
        std::cout << "  1. List all photos\n"
                  << "  2. Search catalog\n"
                  << "  3. Remove entry\n"
                  << "  4. Catalog statistics\n"
                  << "  5. Rate / flag photos\n"
                  << "  6. Add keyword to photo\n"
                  << "  7. Set collection\n"
                  << "  8. Export catalog list\n"
                  << "  0. Back\n\n";
        int ch = promptInt("Choose: ", 0, 8);
        if (ch == 0) break;
        switch (ch) {
        case 1: actionListCatalog();    break;
        case 2: actionSearchCatalog();  break;
        case 3: actionRemoveFromCatalog(); break;
        case 4: actionCatalogStats();   break;
        case 5: actionRatingsManager(); break;
        case 6: {
            uint64_t id = static_cast<uint64_t>(promptInt("Photo ID: ", 1, 999999));
            std::string kw = promptLine("Keyword: ");
            state_.catalog->addKeyword(id, kw);
            state_.catalog->save();
            std::cout << "  Keyword added.\n";
            pauseForInput();
            break;
        }
        case 7: {
            uint64_t id = static_cast<uint64_t>(promptInt("Photo ID: ", 1, 999999));
            std::string col = promptLine("Collection name: ");
            state_.catalog->setCollection(id, col);
            state_.catalog->save();
            std::cout << "  Collection set.\n";
            pauseForInput();
            break;
        }
        case 8: {
            std::string out = promptLine("Output file path: ");
            std::ofstream f(out);
            if (f.is_open()) {
                f << "ID,Path,Camera,DateTaken,Size,Rating\n";
                for (auto& r : state_.catalog->getAll()) {
                    f << r.id << ",\"" << r.path << "\","
                      << "\"" << r.camera_make << " " << r.camera_model << "\","
                      << r.date_taken << "," << r.file_size << "," << r.rating << "\n";
                }
                std::cout << "  Exported to " << out << '\n';
            }
            pauseForInput();
            break;
        }
        }
    }
}

void ConsoleUI::actionListCatalog() {
    auto all = state_.catalog->getAll();
    printHeader("ALL PHOTOS (" + std::to_string(all.size()) + ")");
    bool detailed = promptYesNo("Show detailed info?");
    for (auto& r : all) printPhotoRecord(r, detailed);
    pauseForInput();
}

void ConsoleUI::actionSearchCatalog() {
    printHeader("ADVANCED SEARCH");
    Catalog::SearchFilter f;
    f.camera_model = promptLine("Camera model (blank=any): ");
    f.lens_model   = promptLine("Lens model (blank=any): ");
    f.keyword      = promptLine("Keyword (blank=any): ");
    f.date_from    = promptLine("Date from YYYY-MM-DD (blank=any): ");
    f.date_to      = promptLine("Date to   YYYY-MM-DD (blank=any): ");
    f.collection   = promptLine("Collection (blank=any): ");
    std::string rat = promptLine("Min rating 0-5 (blank=any): ");
    if (!rat.empty()) { try { f.min_rating = std::stoi(rat); } catch (...) {} }
    f.flagged_only = promptYesNo("Flagged only?");

    Filter::SearchQuery q;
    q.filter = f;
    auto res = state_.search->search(q);
    std::cout << "\n  Found " << res.total_count << " photos in "
              << std::fixed << std::setprecision(1) << res.elapsed_ms << " ms:\n";
    for (auto& r : res.photos) printPhotoRecord(r, true);
    pauseForInput();
}

void ConsoleUI::actionRemoveFromCatalog() {
    std::string p = promptLine("Path or ID to remove: ");
    bool removed = false;
    try {
        uint64_t id = std::stoull(p);
        removed = state_.catalog->removePhoto(id);
    } catch (...) {
        removed = state_.catalog->removeByPath(p);
    }
    std::cout << (removed ? "  Removed." : "  Not found.") << '\n';
    state_.catalog->save();
    pauseForInput();
}

void ConsoleUI::actionCatalogStats() {
    printHeader("CATALOG STATISTICS");
    auto all = state_.catalog->getAll();
    uint64_t total_size = 0;
    int has_gps = 0, has_exif = 0;
    for (auto& r : all) {
        total_size += r.file_size;
        if (r.has_gps) ++has_gps;
        if (!r.camera_model.empty()) ++has_exif;
    }
    std::cout << "  Total photos    : " << all.size() << '\n'
              << "  Total size      : " << formatSize(total_size) << '\n'
              << "  With EXIF       : " << has_exif << '\n'
              << "  With GPS        : " << has_gps << '\n'
              << "  Cameras known   : " << state_.catalog->getDistinctCameras().size() << '\n'
              << "  Lenses known    : " << state_.catalog->getDistinctLenses().size() << '\n'
              << "  Keywords        : " << state_.catalog->getDistinctKeywords().size() << '\n'
              << "  Collections     : " << state_.catalog->getDistinctCollections().size() << '\n'
              << "  Hash cache      : " << state_.hasher->cacheSize() << " entries\n";
    pauseForInput();
}

void ConsoleUI::actionRatingsManager() {
    auto all = state_.catalog->getAll();
    if (all.empty()) { std::cout << "  Catalog is empty.\n"; pauseForInput(); return; }
    for (auto& r : all) {
        std::cout << "  [" << r.id << "] " << r.path
                  << " Rating:" << r.rating << (r.flagged?" [FLAGGED]":"") << '\n';
    }
    uint64_t id = static_cast<uint64_t>(promptInt("Photo ID to rate (0=skip): ", 0, 999999));
    if (id == 0) return;
    int rating = promptInt("Rating (0-5): ", 0, 5);
    bool flag  = promptYesNo("Flag this photo?");
    state_.catalog->setRating(id, rating);
    state_.catalog->setFlagged(id, flag);
    state_.catalog->save();
    std::cout << "  Updated.\n";
    pauseForInput();
}


void ConsoleUI::menuNtfsAds() {
    while (true) {
        printHeader("NTFS ALTERNATE DATA STREAMS");
        std::cout << "  Store hashes, tags, ratings, and notes\n"
                  << "  directly in the file (ADS) — no external DB required.\n\n"
                  << "  1. Save SHA-256 hash to file ADS\n"
                  << "  2. Read all ADS metadata from file\n"
                  << "  3. Write a note to the file\n"
                  << "  4. List all file streams\n"
                  << "  5. Copy ADS between files\n"
                  << "  6. Set rating in ADS\n"
                  << "  7. Mark file as verified\n"
                  << "  8. Check NTFS (ADS support)\n"
                  << "  0. Back\n\n";
        int ch = promptInt("Choice: ", 0, 8);
        if (ch == 0) break;
        switch (ch) {
        case 1: actionNtfsWriteHash();  break;
        case 2: actionNtfsReadAll();    break;
        case 3: actionNtfsWriteNote();  break;
        case 4: actionNtfsListStreams(); break;
        case 5: actionNtfsCopyStreams(); break;
        case 6: {
            std::string p = promptLine("File path: ");
            int r = promptInt("Rating (0-5): ", 0, 5);
            bool ok = NTFS::AlternateDataStreams::writeRating(
                FileSystem::Utf8ToWide(p), r);
            std::cout << (ok ? "  Rating saved in ADS." : "  Write error.") << '\n';
            pauseForInput();
            break;
        }
        case 7: {
            std::string p = promptLine("File path: ");
            auto now = std::chrono::system_clock::now();
            auto t   = std::chrono::system_clock::to_time_t(now);
            std::tm tm{}; localtime_s(&tm, &t);
            std::ostringstream ss;
            ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            bool ok = NTFS::AlternateDataStreams::markVerified(
                FileSystem::Utf8ToWide(p), ss.str());
            std::cout << (ok ? "  File marked as verified." : "  Error.") << '\n';
            pauseForInput();
            break;
        }
        case 8: {
            std::string p = promptLine("Path (file or folder): ");
            bool ntfs = NTFS::AlternateDataStreams::isNtfsVolume(
                FileSystem::Utf8ToWide(p));
            std::cout << "\n  Volume " << (ntfs ? "is NTFS — ADS supported."
                                               : "is NOT NTFS — ADS unavailable.") << '\n';
            pauseForInput();
            break;
        }
        }
    }
}

void ConsoleUI::actionNtfsWriteHash() {
    std::string p = promptLine("File path: ");
    if (p.empty()) return;
    std::wstring wp = FileSystem::Utf8ToWide(p);
    try {
        std::string hex = state_.hasher->computeFileHash(wp);
        bool ok = NTFS::AlternateDataStreams::writeHash(wp, hex);
        std::cout << "\n  SHA-256: " << hex << '\n'
                  << "  ADS write: " << (ok ? "OK" : "FAILED") << '\n';
        state_.oplog->log(Log::OpType::HASH_COMPUTE, p, "", hex);
    } catch (const std::exception& e) {
        std::cout << "  Error: " << e.what() << '\n';
    }
    pauseForInput();
}

void ConsoleUI::actionNtfsReadAll() {
    std::string p = promptLine("File path: ");
    if (p.empty()) return;
    auto meta = NTFS::AlternateDataStreams::readAllMetadata(FileSystem::Utf8ToWide(p));
    printHeader("ADS METADATA");
    if (meta.empty()) {
        std::cout << "  PhotoManager ADS not found.\n";
    } else {
        for (auto& [k, v] : meta) {
            std::cout << "  [" << k << "]\n    " << v << '\n';
        }
    }
    auto stored_hash = NTFS::AlternateDataStreams::readHash(FileSystem::Utf8ToWide(p));
    if (stored_hash) {
        std::cout << "\n  Stored hash: " << *stored_hash << '\n';
        try {
            std::string actual = state_.hasher->computeFileHash(FileSystem::Utf8ToWide(p));
            bool intact = actual == *stored_hash;
            std::cout << "  Current hash:   " << actual << '\n'
                      << "  Integrity:     " << (intact ? "✓ File unchanged"
                                                          : "✗ File modified!") << '\n';
        } catch (...) {}
    }
    pauseForInput();
}

void ConsoleUI::actionNtfsWriteNote() {
    std::string p = promptLine("File path: ");
    if (p.empty()) return;
    auto existing = NTFS::AlternateDataStreams::readNote(FileSystem::Utf8ToWide(p));
    if (existing && !existing->empty())
        std::cout << "  Current note: " << *existing << '\n';
    std::string note = promptLine("New note: ");
    bool ok = NTFS::AlternateDataStreams::writeNote(FileSystem::Utf8ToWide(p), note);
    std::cout << (ok ? "  Note saved." : "  Write error.") << '\n';
    pauseForInput();
}

void ConsoleUI::actionNtfsListStreams() {
    std::string p = promptLine("File path: ");
    if (p.empty()) return;
    auto streams = NTFS::AlternateDataStreams::listStreams(FileSystem::Utf8ToWide(p));
    printHeader("NTFS STREAMS");
    if (streams.empty()) {
        std::cout << "  No additional streams.\n";
    } else {
        for (auto& s : streams) {
            std::cout << "  :" << FileSystem::WideToUtf8(s.stream_name)
                      << "  (" << formatSize(s.size) << ")\n";
            if (!s.data.empty()) {
                std::string preview = s.data.substr(0, 80);
                std::cout << "    " << preview
                          << (s.data.size() > 80 ? "..." : "") << '\n';
            }
        }
    }
    pauseForInput();
}

void ConsoleUI::actionNtfsCopyStreams() {
    std::string src = promptLine("Source (file with ADS): ");
    std::string dst = promptLine("Target (file): ");
    if (src.empty() || dst.empty()) return;
    bool ok = NTFS::AlternateDataStreams::copyStreams(
        FileSystem::Utf8ToWide(src), FileSystem::Utf8ToWide(dst));
    std::cout << (ok ? "  ADS copied." : "  ADS copy error.") << '\n';
    pauseForInput();
}

// ============================================================
//  Folder Watcher
// ============================================================

void ConsoleUI::menuWatcher() {
    while (true) {
        printHeader("FOLDER WATCHER (ReadDirectoryChangesW)");
        bool running = state_.watcher->isRunning();
        std::cout << "  Status: " << (running ? "ACTIVE — " : "Stopped")
                  << (running ? FileSystem::WideToUtf8(state_.watcher->watchedFolder()) : "")
                  << "\n\n"
                  << "  1. Start watching\n"
                  << "  2. Fetch events (poll)\n"
                  << "  3. Stop watching\n"
                  << "  0. Back\n\n";
        int ch = promptInt("Choice: ", 0, 3);
        if (ch == 0) break;
        switch (ch) {
        case 1: actionWatcherStart(); break;
        case 2: actionWatcherPoll();  break;
        case 3:
            state_.watcher->stopWatching();
            std::cout << "  Watching stopped.\n";
            pauseForInput();
            break;
        }
    }
}

void ConsoleUI::actionWatcherStart() {
    if (state_.watcher->isRunning()) {
        std::cout << "  Already active. Stop it first.\n";
        pauseForInput(); return;
    }
    std::string folder = state_.current_folder.empty()
        ? promptFolder("Folder to watch")
        : state_.current_folder;

    bool recursive = promptYesNo("Include subfolders?");

    state_.watcher->setCallback([this](const Watch::FileChangeEvent& ev) {
        const wchar_t* types[] = {
            L"ADDED", L"REMOVED", L"MODIFIED",
            L"RENAMED_FROM", L"RENAMED_TO",
            L"DIR_ADDED", L"DIR_REMOVED"
        };
        int idx = static_cast<int>(ev.type);
        std::wstring type_name = (idx >= 0 && idx < 7) ? types[idx] : L"UNKNOWN";
        std::cout << "\n  [WATCHER] " << FileSystem::WideToUtf8(type_name)
                  << ": " << FileSystem::WideToUtf8(ev.path) << '\n';

        if (ev.type == Watch::ChangeType::FILE_ADDED) {
            state_.oplog->log(Log::OpType::IMPORT,
                FileSystem::WideToUtf8(ev.path), "", "auto-detected");
        }
    });

    bool ok = state_.watcher->startWatching(
        FileSystem::Utf8ToWide(folder), recursive, true);
    std::cout << (ok ? "  Watching started. New photos will appear automatically."
                     : "  Failed to start watcher.") << '\n';
    pauseForInput();
}

void ConsoleUI::actionWatcherPoll() {
    auto events = state_.watcher->pollEvents();
    printHeader("NEW EVENTS");
    if (events.empty()) {
        std::cout << "  No new events.\n";
    } else {
        const char* types[] = {
            "ADDED","REMOVED","MODIFIED",
            "RENAMED_FROM","RENAMED_TO","DIR_ADDED","DIR_REMOVED"
        };
        for (auto& ev : events) {
            int idx = static_cast<int>(ev.type);
            std::cout << "  [" << (idx>=0&&idx<7 ? types[idx] : "?") << "] "
                      << FileSystem::WideToUtf8(ev.path) << '\n';
        }
    }
    pauseForInput();
}

// ============================================================
//  Operation Log
// ============================================================

void ConsoleUI::menuOperationLog() {
    while (true) {
        printHeader("OPERATION LOG");
        std::cout << "  Total entries: " << state_.oplog->count() << "\n\n"
                  << "  1. Show recent operations\n"
                  << "  2. Undo last operation\n"
                  << "  3. Undo by ID\n"
                  << "  4. Operation statistics\n"
                  << "  5. Clear log\n"
                  << "  0. Back\n\n";
        int ch = promptInt("Choice: ", 0, 5);
        if (ch == 0) break;
        switch (ch) {
        case 1: actionLogShow();  break;
        case 2: actionLogUndo();  break;
        case 3: {
            int id = promptInt("Operation ID: ", 1, 999999);
            bool ok = state_.oplog->undo(static_cast<uint64_t>(id));
            std::cout << (ok ? "  Undo successful." : "  Failed to undo.") << '\n';
            pauseForInput();
            break;
        }
        case 4: actionLogStats(); break;
        case 5:
            if (promptYesNo("Clear log?")) {
                state_.oplog->clear();
                std::cout << "  Log cleared.\n";
            }
            pauseForInput();
            break;
        }
    }
}

void ConsoleUI::actionLogShow() {
    auto recent = state_.oplog->getRecent(30);
    printHeader("RECENT OPERATIONS");
    if (recent.empty()) {
        std::cout << "  Log is empty.\n";
    } else {
        for (auto& e : recent) {
            std::cout << "  [" << std::setw(5) << e.id << "] "
                      << std::left << std::setw(12) << state_.oplog->opTypeName(e.type)
                      << " " << e.timestamp
                      << (e.undone ? " [UNDONE]" : "")
                      << (e.undoable && !e.undone ? " [↩undo]" : "") << '\n'
                      << "           Src: " << e.source_path.substr(0, 55) << '\n';
            if (!e.dest_path.empty())
                std::cout << "           Dst: " << e.dest_path.substr(0, 55) << '\n';
        }
    }
    pauseForInput();
}

void ConsoleUI::actionLogUndo() {
    bool ok = state_.oplog->undoLast();
    std::cout << (ok ? "  Last operation undone." : "  Nothing to undo.") << '\n';
    pauseForInput();
}

void ConsoleUI::actionLogStats() {
    printHeader("OPERATION STATISTICS");
    std::cout << state_.oplog->statistics();
    pauseForInput();
}

// ============================================================
//  Thumbnails
// ============================================================

void ConsoleUI::menuThumbnails() {
    while (true) {
        printHeader("THUMBNAILS");
        std::cout << "  1. Generate file thumbnail\n"
                  << "  2. ASCII preview in console\n"
                  << "  3. Thumbnail cache info\n"
                  << "  4. Open thumbnail\n"
                  << "  5. Generate for all files in folder\n"
                  << "  6. Clear thumbnail cache\n"
                  << "  0. Back\n\n";
        int ch = promptInt("Choice: ", 0, 6);
        if (ch == 0) break;
        switch (ch) {
        case 1: actionThumbGenerate();     break;
        case 2: actionThumbPreviewAscii(); break;
        case 3: actionThumbCacheInfo();    break;
        case 4: {
            std::string p = promptLine("Photo path: ");
            auto t = state_.thumbs->getThumbnail(FileSystem::Utf8ToWide(p));
            if (t) FileSystem::OpenWithDefaultApp(*t);
            else std::cout << "  Failed to retrieve thumbnail.\n";
            pauseForInput();
            break;
        }
        case 5: {
            auto files = scanCurrentFolder(false);
            std::cout << "\n  Generating for " << files.size() << " files...\n";
            int ok=0, fail=0;
            for (auto& fi : files) {
                std::wstring tp;
                if (state_.thumbs->generateThumbnail(fi.full_path, tp)) ++ok;
                else ++fail;
                std::cout << "\r  OK:" << ok << " FAIL:" << fail << std::flush;
            }
            std::cout << '\n';
            pauseForInput();
            break;
        }
        case 6:
            state_.thumbs->clearCache();
            std::cout << "  Cache cleared.\n";
            pauseForInput();
            break;
        }
    }
}

void ConsoleUI::actionThumbGenerate() {
    std::string p = promptLine("Photo path: ");
    if (p.empty()) return;
    std::wstring out;
    bool ok = state_.thumbs->generateThumbnail(FileSystem::Utf8ToWide(p), out);
    if (ok)
        std::cout << "  Thumbnail: " << FileSystem::WideToUtf8(out) << '\n';
    else
        std::cout << "  Failed to generate thumbnail.\n";
    pauseForInput();
}

void ConsoleUI::actionThumbPreviewAscii() {
    std::string p = promptLine("JPEG path: ");
    if (p.empty()) return;
    state_.thumbs->generateAsciiPreview(FileSystem::Utf8ToWide(p), 62, 28);
    pauseForInput();
}

void ConsoleUI::actionThumbCacheInfo() {
    printHeader("THUMBNAIL CACHE");
    std::cout << "  Count: " << state_.thumbs->cacheCount() << '\n'
              << "  Size:    " << formatSize(state_.thumbs->cacheSizeBytes()) << '\n'
              << "  Dimensions:    " << state_.thumbs->thumbSize() << " px\n";
    pauseForInput();
}
}
// namespace PhotoManager::UI

// ============================================================
//  NTFS Alternate Data Streams
// ============================================================