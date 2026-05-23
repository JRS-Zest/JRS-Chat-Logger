// guild_member_processor.h — ギルド員一覧パケット (0x1128 / 0x1203) 解析
#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <chrono>
#include <mutex>
#include <map>

// ギルド員1名分の情報
struct GuildMemberInfo {
    std::wstring name;       // ギルド員名 (Shift_JIS → wstring)
    int          level = 0;
    uint8_t      jobCode = 0;   // 職業コード (未マッピング: 数値表示)
    uint8_t      rankCode = 0;  // 役職コード
    std::wstring jobName;       // 変換済み職業名 (未マッピング時は数値文字列)
    std::wstring rankName;      // 変換済み役職名
};

// ギルド員一覧 (1回のパケットで得られる全データ)
struct GuildMemberResult {
    std::chrono::system_clock::time_point timestamp;
    std::wstring guildName;
    std::vector<GuildMemberInfo> members;
};

class GuildMemberProcessor {
public:
    using GuildCallback = std::function<void(const GuildMemberResult&)>;

    GuildMemberProcessor();

    void SetGuildCallback(GuildCallback cb) { callback_ = std::move(cb); }

    // パケットを走査し、0x1128 コンテナ内の 0x1203 を検出・解析する
    void ProcessPacket(const uint8_t* payload, int len, bool isIncoming, int srcPort, int dstPort);

    // CSV保存 (ログ\ギルド員\ に保存。同一ギルド名で指定時間以内ならスキップ)
    // exeDir: 実行ファイルディレクトリ, intervalHours: 自動保存間隔(時間)
    void SaveCsvIfNeeded(const std::wstring& exeDir, const GuildMemberResult& result, int intervalHours = 72);

    // 強制CSV保存 (間隔制限無視)
    void ForceSaveCsv(const std::wstring& exeDir, const GuildMemberResult& result);

private:
    void AnalyzeOuterPacket(const uint8_t* data, int len);

    // Shift_JIS バイト列 → wstring 変換
    static std::wstring SjisToWide(const uint8_t* data, int maxLen);

    // 職業コード → 名前
    static std::wstring JobToName(uint8_t job);
    // 役職コード → 名前
    static std::wstring RankToName(uint8_t rank);

    GuildCallback callback_;

    // パケット再結合バッファ
    std::vector<uint8_t> reassemblyBuf_;  // 蓄積バッファ
    uint16_t expectedLen_ = 0;            // TotalLength (全体パケット長)
    bool waitingHeader_ = true;           // ヘッダー待ち状態
    void ProcessReassembledPacket(const uint8_t* data, int len);

    // クロスパケット蓄積 (ギルド員一覧とギルド名が別パケットで届く)
    std::vector<GuildMemberInfo> pendingMembers_;
    std::wstring pendingGuildName_;
    std::chrono::system_clock::time_point pendingTimestamp_;
    void TryFireCallback();

    // 保存制限: ギルド名 → 最終保存時刻
    std::map<std::wstring, std::chrono::system_clock::time_point> lastSaveMap_;
    std::mutex saveMutex_;

    // ギルド員フォルダ内の既存ファイルから最終保存時刻を復元
    void LoadLastSaveTimesFromFolder(const std::wstring& folderPath);
    bool folderScanned_ = false;
};
