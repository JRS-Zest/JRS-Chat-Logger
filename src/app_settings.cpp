#include "app_settings.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <string>
#include <locale>
#include <codecvt>

// --- Helpers ---
static std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring p(path);
    auto pos = p.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? p.substr(0, pos) : L".";
}

static std::wstring Trim(const std::wstring& s) {
    auto start = s.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    auto end = s.find_last_not_of(L" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool ParseBool(const std::wstring& val, bool defaultVal) {
    auto v = Trim(val);
    if (v == L"True" || v == L"true" || v == L"1") return true;
    if (v == L"False" || v == L"false" || v == L"0") return false;
    return defaultVal;
}

static float ParseFloat(const std::wstring& val, float defaultVal) {
    try {
        return std::stof(val);
    } catch (...) {
        return defaultVal;
    }
}

static int ParseInt(const std::wstring& val, int defaultVal) {
    try {
        return std::stoi(val);
    } catch (...) {
        return defaultVal;
    }
}

static std::wstring BoolToStr(bool b) {
    return b ? L"True" : L"False";
}

// --- Implementation ---
std::wstring AppSettings::GetConfigPath() {
    return GetExeDirectory() + L"\\logger_config.ini";
}

void AppSettings::Load() {
    auto path = GetConfigPath();

    std::wifstream file(path);
    if (!file.is_open()) return;

    // UTF-8 BOM対応
    file.imbue(std::locale(file.getloc(), new std::codecvt_utf8_utf16<wchar_t, 0x10FFFF, std::consume_header>));

    std::wstring currentSection;
    std::wstring line;
    while (std::getline(file, line)) {
        auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#') continue;

        if (trimmed.front() == L'[' && trimmed.back() == L']') {
            currentSection = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }

        auto eqPos = trimmed.find(L'=');
        if (eqPos == std::wstring::npos) continue;

        auto key = Trim(trimmed.substr(0, eqPos));
        auto value = Trim(trimmed.substr(eqPos + 1));

        SetValue(currentSection, key, value);
    }

    ValidateGeometry();
}

void AppSettings::SetValue(const std::wstring& section, const std::wstring& key, const std::wstring& value) {
    if (section == L"GUI") {
        if (key == L"geometry")      geometry = value;
        else if (key == L"selected_tabs") selectedTabs = value;
        else if (key == L"iface_name")    interfaceName = value;
    }
    else if (section == L"Server") {
        if (key == L"target_server_ip") targetServerIP = value;
    }
    else if (section == L"Data") {
        if (key == L"redstone_dat_path") redStoneDatPath = value;
        else if (key == L"map_path")     mapPath = value;
    }
    else if (section == L"Sound") {
        if (key == L"item_notification_enabled")      itemNotificationEnabled = ParseBool(value, true);
        else if (key == L"option_notification_enabled") optionNotificationEnabled = ParseBool(value, true);
        else if (key == L"inv_full_notification_enabled") invFullNotificationEnabled = ParseBool(value, true);
        else if (key == L"bell_notification_enabled")   bellNotificationEnabled = ParseBool(value, true);
        else if (key == L"whisper_notification_enabled") whisperNotificationEnabled = ParseBool(value, true);
        else if (key == L"drop_notification_enabled")    dropNotificationEnabled = ParseBool(value, true);
        else if (key == L"drop_dc_folder")               dropDcFolder = value;
        else if (key == L"sound_volume")                soundVolume = ParseFloat(value, 0.5f);
    }
    else if (section == L"Bell") {
        if (key == L"bell_last_utc") lastBellUtc = value;
    }
    else if (section == L"Log") {
        if (key == L"max_lines_per_file") maxLinesPerFile = ParseInt(value, 1000);
        else if (key == L"income_start_utc") incomeStartUtc = value;
    }
    else if (section == L"DC") {
        if (key == L"dc_profile") dcProfile = value;
    }
    else if (section == L"Guild") {
        if (key == L"guild_save_interval_hours") guildSaveIntervalHours = ParseInt(value, 72);
    }
}

void AppSettings::ValidateGeometry() {
    try {
        auto base = geometry;
        auto plusPos = base.find(L'+');
        if (plusPos != std::wstring::npos) base = base.substr(0, plusPos);
        auto xPos = base.find(L'x');
        if (xPos != std::wstring::npos) {
            int w = std::stoi(base.substr(0, xPos));
            int h = std::stoi(base.substr(xPos + 1));
            if (w < 200 || h < 100) geometry = L"600x350+50+50";
        }
    } catch (...) {
        geometry = L"600x350+50+50";
    }
}

void AppSettings::Save() const {
    auto path = GetConfigPath();

    std::wofstream file(path);
    if (!file.is_open()) return;

    file.imbue(std::locale(file.getloc(), new std::codecvt_utf8_utf16<wchar_t, 0x10FFFF, std::generate_header>));

    // GUI セクション
    file << L"[GUI]\n";
    file << L"geometry = " << geometry << L"\n";
    file << L"selected_tabs = " << selectedTabs << L"\n";
    file << L"iface_name = " << interfaceName << L"\n";
    file << L"\n";

    // Server セクション
    file << L"[Server]\n";
    file << L"; JRS専用IP\n";
    file << L"target_server_ip = " << targetServerIP << L"\n";
    file << L"\n";

    // Data セクション
    file << L"[Data]\n";
    file << L"redstone_dat_path = " << redStoneDatPath << L"\n";
    file << L"map_path = " << mapPath << L"\n";
    file << L"\n";

    // Sound セクション
    file << L"[Sound]\n";
    file << L"item_notification_enabled = " << BoolToStr(itemNotificationEnabled) << L"\n";
    file << L"option_notification_enabled = " << BoolToStr(optionNotificationEnabled) << L"\n";
    file << L"inv_full_notification_enabled = " << BoolToStr(invFullNotificationEnabled) << L"\n";
    file << L"bell_notification_enabled = " << BoolToStr(bellNotificationEnabled) << L"\n";
    file << L"whisper_notification_enabled = " << BoolToStr(whisperNotificationEnabled) << L"\n";
    file << L"drop_notification_enabled = " << BoolToStr(dropNotificationEnabled) << L"\n";
    file << L"drop_dc_folder = " << dropDcFolder << L"\n";
    file << L"sound_volume = " << soundVolume << L"\n";
    file << L"\n";

    // Log セクション
    file << L"[Log]\n";
    file << L"max_lines_per_file = " << maxLinesPerFile << L"\n";
    if (!incomeStartUtc.empty()) {
        file << L"income_start_utc = " << incomeStartUtc << L"\n";
    }
    file << L"\n";

    // Bell セクション
    file << L"[Bell]\n";
    if (!lastBellUtc.empty()) {
        file << L"bell_last_utc = " << lastBellUtc << L"\n";
    }
    file << L"\n";

    // DC セクション
    file << L"[DC]\n";
    file << L"dc_profile = " << dcProfile << L"\n";
    file << L"\n";

    // Guild セクション
    file << L"[Guild]\n";
    file << L"guild_save_interval_hours = " << guildSaveIntervalHours << L"\n";
}

void AppSettings::EnsureDefaultConfig() {
    auto path = GetConfigPath();
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return; // exists
    Save();
}

void AppSettings::SaveInterfaceConfig(const std::wstring& iface) {
    interfaceName = iface;
    Save();
}

void AppSettings::SaveSoundConfig() {
    Save();
}

void AppSettings::SaveGuiDisplayConfig(const std::wstring& geom, const std::wstring& tabs) {
    geometry = geom;
    selectedTabs = tabs;
    Save();
}

void AppSettings::SaveGuildConfig() {
    Save();
}
