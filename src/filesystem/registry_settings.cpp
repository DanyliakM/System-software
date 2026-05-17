#include "registry_settings.hpp"
#include "file_ops.hpp"
#include <algorithm>
#include <sstream>

namespace PhotoManager::Settings {

RegistrySettings::RegistrySettings(HKEY root) : root_(root) {}

RegistrySettings::~RegistrySettings() { close(); }

bool RegistrySettings::open(bool create_if_missing) {
    if (open_) return true;
    DWORD disposition;
    LONG res = RegCreateKeyExW(root_, APP_KEY, 0, nullptr,
                                REG_OPTION_NON_VOLATILE,
                                KEY_ALL_ACCESS, nullptr,
                                &hkey_, &disposition);
    if (res != ERROR_SUCCESS) {
        if (!create_if_missing) return false;
        res = RegOpenKeyExW(root_, APP_KEY, 0, KEY_ALL_ACCESS, &hkey_);
        if (res != ERROR_SUCCESS) return false;
    }
    open_ = true;
    return true;
}

void RegistrySettings::close() {
    if (open_ && hkey_) {
        RegCloseKey(hkey_);
        hkey_ = nullptr;
        open_ = false;
    }
}

bool RegistrySettings::isOpen() const { return open_; }

bool RegistrySettings::setString(const std::wstring& name, const std::wstring& value) {
    if (!open_) return false;
    LONG res = RegSetValueExW(hkey_, name.c_str(), 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(value.c_str()),
                               static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    return res == ERROR_SUCCESS;
}

std::optional<std::wstring> RegistrySettings::getString(const std::wstring& name) const {
    if (!open_) return std::nullopt;
    DWORD type, size = 0;
    LONG res = RegQueryValueExW(hkey_, name.c_str(), nullptr,
                                 &type, nullptr, &size);
    if (res != ERROR_SUCCESS || type != REG_SZ) return std::nullopt;

    std::wstring value(size / sizeof(wchar_t), L'\0');
    res = RegQueryValueExW(hkey_, name.c_str(), nullptr,
                            &type, reinterpret_cast<BYTE*>(value.data()), &size);
    if (res != ERROR_SUCCESS) return std::nullopt;
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return value;
}

bool RegistrySettings::setInt(const std::wstring& name, DWORD value) {
    if (!open_) return false;
    LONG res = RegSetValueExW(hkey_, name.c_str(), 0, REG_DWORD,
                               reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
    return res == ERROR_SUCCESS;
}

std::optional<DWORD> RegistrySettings::getInt(const std::wstring& name) const {
    if (!open_) return std::nullopt;
    DWORD value, type, size = sizeof(DWORD);
    LONG res = RegQueryValueExW(hkey_, name.c_str(), nullptr, &type,
                                 reinterpret_cast<BYTE*>(&value), &size);
    if (res != ERROR_SUCCESS || type != REG_DWORD) return std::nullopt;
    return value;
}

bool RegistrySettings::setBool(const std::wstring& name, bool value) {
    return setInt(name, value ? 1u : 0u);
}

bool RegistrySettings::getBool(const std::wstring& name, bool default_val) const {
    auto v = getInt(name);
    return v.has_value() ? (*v != 0) : default_val;
}

bool RegistrySettings::setStringList(const std::wstring& name,
                                      const std::vector<std::wstring>& list)
{
    std::wstring multi;
    for (auto& s : list) { multi += s; multi += L'\0'; }
    multi += L'\0';
    if (!open_) return false;
    LONG res = RegSetValueExW(hkey_, name.c_str(), 0, REG_MULTI_SZ,
                               reinterpret_cast<const BYTE*>(multi.data()),
                               static_cast<DWORD>(multi.size() * sizeof(wchar_t)));
    return res == ERROR_SUCCESS;
}

std::vector<std::wstring> RegistrySettings::getStringList(const std::wstring& name) const {
    if (!open_) return {};
    DWORD type, size = 0;
    LONG res = RegQueryValueExW(hkey_, name.c_str(), nullptr, &type, nullptr, &size);
    if (res != ERROR_SUCCESS || type != REG_MULTI_SZ) return {};

    std::vector<wchar_t> buf(size / sizeof(wchar_t), L'\0');
    res = RegQueryValueExW(hkey_, name.c_str(), nullptr, &type,
                            reinterpret_cast<BYTE*>(buf.data()), &size);
    if (res != ERROR_SUCCESS) return {};

    std::vector<std::wstring> result;
    const wchar_t* p = buf.data();
    while (p && *p) {
        result.emplace_back(p);
        p += result.back().size() + 1;
    }
    return result;
}

bool RegistrySettings::deleteValue(const std::wstring& name) {
    if (!open_) return false;
    return RegDeleteValueW(hkey_, name.c_str()) == ERROR_SUCCESS;
}

bool RegistrySettings::hasValue(const std::wstring& name) const {
    if (!open_) return false;
    return RegQueryValueExW(hkey_, name.c_str(), nullptr,
                             nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

std::vector<std::wstring> RegistrySettings::listValues() const {
    if (!open_) return {};
    std::vector<std::wstring> names;
    DWORD idx = 0;
    wchar_t buf[256];
    DWORD bufsz;
    while (true) {
        bufsz = 256;
        LONG res = RegEnumValueW(hkey_, idx++, buf, &bufsz,
                                  nullptr, nullptr, nullptr, nullptr);
        if (res != ERROR_SUCCESS) break;
        names.emplace_back(buf, bufsz);
    }
    return names;
}

void RegistrySettings::saveLastOpenedFolder(const std::wstring& path) {
    setString(L"LastOpenedFolder", path);
}

std::wstring RegistrySettings::getLastOpenedFolder() const {
    auto v = getString(L"LastOpenedFolder");
    return v.value_or(L"");
}

void RegistrySettings::saveRecentFolders(const std::vector<std::wstring>& folders) {
    setStringList(L"RecentFolders", folders);
}

std::vector<std::wstring> RegistrySettings::getRecentFolders() const {
    return getStringList(L"RecentFolders");
}

void RegistrySettings::addRecentFolder(const std::wstring& path, size_t max_entries) {
    auto recent = getRecentFolders();
    recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
    recent.insert(recent.begin(), path);
    if (recent.size() > max_entries) recent.resize(max_entries);
    saveRecentFolders(recent);
}

void RegistrySettings::saveWindowLayout(int x, int y, int w, int h, bool maximized) {
    setInt(L"WindowX", static_cast<DWORD>(x));
    setInt(L"WindowY", static_cast<DWORD>(y));
    setInt(L"WindowW", static_cast<DWORD>(w));
    setInt(L"WindowH", static_cast<DWORD>(h));
    setBool(L"WindowMaximized", maximized);
}

RegistrySettings::WindowLayout RegistrySettings::getWindowLayout() const {
    WindowLayout wl{100, 100, 1024, 768, false};
    if (auto v = getInt(L"WindowX")) wl.x = static_cast<int>(*v);
    if (auto v = getInt(L"WindowY")) wl.y = static_cast<int>(*v);
    if (auto v = getInt(L"WindowW")) wl.w = static_cast<int>(*v);
    if (auto v = getInt(L"WindowH")) wl.h = static_cast<int>(*v);
    wl.maximized = getBool(L"WindowMaximized", false);
    return wl;
}

void RegistrySettings::savePreference(const std::string& key, const std::string& value) {
    setString(FileSystem::Utf8ToWide(key), FileSystem::Utf8ToWide(value));
}

std::string RegistrySettings::getPreference(const std::string& key,
                                              const std::string& default_val) const
{
    auto v = getString(FileSystem::Utf8ToWide(key));
    return v.has_value() ? FileSystem::WideToUtf8(*v) : default_val;
}

void RegistrySettings::saveImportOptions(const std::string& dest,
                                          int organize_mode,
                                          bool skip_dupes,
                                          bool compute_hash)
{
    setString(L"ImportDest", FileSystem::Utf8ToWide(dest));
    setInt(L"ImportOrganizeMode", static_cast<DWORD>(organize_mode));
    setBool(L"ImportSkipDupes", skip_dupes);
    setBool(L"ImportComputeHash", compute_hash);
}

RegistrySettings::ImportPrefs RegistrySettings::getImportOptions() const {
    ImportPrefs p;
    auto dest = getString(L"ImportDest");
    p.dest = dest.has_value() ? FileSystem::WideToUtf8(*dest) : "";
    p.organize_mode  = static_cast<int>(getInt(L"ImportOrganizeMode").value_or(1));
    p.skip_dupes     = getBool(L"ImportSkipDupes", true);
    p.compute_hash   = getBool(L"ImportComputeHash", true);
    return p;
}

bool RegistrySettings::writeToRegistry(HKEY root, const std::wstring& key,
                                         const std::wstring& name,
                                         const std::wstring& value)
{
    HKEY hkey;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                         nullptr, &hkey, nullptr) != ERROR_SUCCESS)
        return false;
    LONG res = RegSetValueExW(hkey, name.c_str(), 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(value.c_str()),
                               static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
    RegCloseKey(hkey);
    return res == ERROR_SUCCESS;
}

std::optional<std::wstring> RegistrySettings::readFromRegistry(
    HKEY root, const std::wstring& key, const std::wstring& name)
{
    HKEY hkey;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS)
        return std::nullopt;
    DWORD type, size = 0;
    RegQueryValueExW(hkey, name.c_str(), nullptr, &type, nullptr, &size);
    if (type != REG_SZ) { RegCloseKey(hkey); return std::nullopt; }
    std::wstring value(size/sizeof(wchar_t), L'\0');
    RegQueryValueExW(hkey, name.c_str(), nullptr, &type,
                      reinterpret_cast<BYTE*>(value.data()), &size);
    RegCloseKey(hkey);
    while (!value.empty() && value.back()==L'\0') value.pop_back();
    return value;
}

} // namespace PhotoManager::Settings