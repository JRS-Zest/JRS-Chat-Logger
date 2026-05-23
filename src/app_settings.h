#pragma once
#include <string>
#include <optional>
#include <chrono>

// C# AppSettings.cs の完全移植
// logger_config.ini (INI形式) を読み書きする

class AppSettings {
public:
    // --- GUI設定 ---
    std::wstring geometry = L"600x400+0+0";
    std::wstring selectedTabs;
    std::wstring interfaceName; // C# iface_name
    std::wstring dcProfile;

    // --- サーバー設定 ---
    std::wstring targetServerIP = L"220.153.168.131";

    // --- Data設定 ---
    std::wstring redStoneDatPath;
    std::wstring mapPath;

    // --- サウンド設定 ---
    bool itemNotificationEnabled = true;
    bool invFullNotificationEnabled = true;
    bool bellNotificationEnabled = true;
    bool whisperNotificationEnabled = true;
    bool optionNotificationEnabled = true;
    bool dropNotificationEnabled = true;
    std::wstring dropDcFolder = L"Ultimate";
    float soundVolume = 0.5f;

    // --- Bell設定 ---
    std::wstring lastBellUtc; // ISO8601文字列 (空 = なし)

    // --- ログ設定 ---
    int maxLinesPerFile = 1000;
    std::wstring incomeStartUtc; // ISO8601文字列 (空 = なし)

    // --- ギルド設定 ---
    int guildSaveIntervalHours = 72;  // 自動保存間隔 (時間): 24=1日, 72=3日, 168=1週間, 720=1ヶ月

    // --- DC設定 ---
    // (dcProfile は GUI セクションに含む)

    // ファイル操作
    void Load();
    void Save() const;
    void EnsureDefaultConfig();

    // 設定ファイルパス取得
    static std::wstring GetConfigPath();

    // ユーティリティ
    void SaveInterfaceConfig(const std::wstring& iface);
    void SaveSoundConfig();
    void SaveGuiDisplayConfig(const std::wstring& geom, const std::wstring& tabs);
    void SaveGuildConfig();

private:
    void SetValue(const std::wstring& section, const std::wstring& key, const std::wstring& value);
    void ValidateGeometry();
};
