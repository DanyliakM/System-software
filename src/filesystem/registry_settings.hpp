#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace PhotoManager::Settings {

class RegistrySettings {
public:
    static constexpr const wchar_t* APP_KEY =
        L"SOFTWARE\\PhotoManager";

    explicit RegistrySettings(HKEY root = HKEY_CURRENT_USER);
    ~RegistrySettings();

    bool open(bool create_if_missing = true);
    void close();
    bool isOpen() const;

    bool        setString(const std::wstring& name, const std::wstring& value);
    std::optional<std::wstring> getString(const std::wstring& name) const;

    bool        setInt(const std::wstring& name, DWORD value);
    std::optional<DWORD> getInt(const std::wstring& name) const;

    bool        setBool(const std::wstring& name, bool value);
    bool        getBool(const std::wstring& name, bool default_val = false) const;

    bool        setStringList(const std::wstring& name,
                               const std::vector<std::wstring>& list);
    std::vector<std::wstring> getStringList(const std::wstring& name) const;

    bool        deleteValue(const std::wstring& name);
    bool        hasValue(const std::wstring& name) const;

    std::vector<std::wstring> listValues() const;

    void        saveLastOpenedFolder(const std::wstring& path);
    std::wstring getLastOpenedFolder() const;

    void        saveRecentFolders(const std::vector<std::wstring>& folders);
    std::vector<std::wstring> getRecentFolders() const;
    void        addRecentFolder(const std::wstring& path, size_t max_entries = 10);

    void        saveWindowLayout(int x, int y, int w, int h, bool maximized);
    struct WindowLayout { int x,y,w,h; bool maximized; };
    WindowLayout getWindowLayout() const;

    void        savePreference(const std::string& key, const std::string& value);
    std::string getPreference(const std::string& key,
                               const std::string& default_val = "") const;

    void        saveImportOptions(const std::string& dest,
                                   int organize_mode,
                                   bool skip_dupes,
                                   bool compute_hash);
    struct ImportPrefs { std::string dest; int organize_mode; bool skip_dupes, compute_hash; };
    ImportPrefs getImportOptions() const;

    static bool writeToRegistry(HKEY root, const std::wstring& key,
                                  const std::wstring& name,
                                  const std::wstring& value);
    static std::optional<std::wstring> readFromRegistry(
        HKEY root, const std::wstring& key, const std::wstring& name);

private:
    HKEY root_;
    HKEY hkey_{nullptr};
    bool open_{false};
};

} // namespace PhotoManager::Settings