#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ItemDatLoader;

// フリマ (0x1128 / 0x12E4) の取得・CSV保存・PriceViewer用JSONL保存
// 既存の rate_roten_processor.cpp とは独立して動作する
class FleaMarketProcessor {
public:
    explicit FleaMarketProcessor(ItemDatLoader& itemDatLoader, int maxLinesPerFile = 1000);

    // パケット処理 (キャプチャスレッドから呼ばれる)
    void ProcessPacket(const uint8_t* data, int len, bool isIncoming, int srcPort, int dstPort, uint32_t seq);

private:
    struct Record {
        uint32_t serial = 0;
        uint32_t price = 0;
        uint16_t currency = 0;
        std::wstring seller;
        uint16_t itemId = 0;
        uint8_t quantity = 1;
        struct Option {
            uint16_t id = 0;
            uint16_t value = 0;
        };
        std::vector<Option> options;
    };

    struct FileState {
        std::wstring dir;
        std::wstring prefix;
        std::wstring extension;
        int date[3] = {0, 0, 0};
        bool dateValid = false;
        int fileIndex = 0;
        int lineCount = 0;
        std::wstring filePath;
    };

    ItemDatLoader& itemDatLoader_;
    int maxLinesPerFile_;
    FileState csvState_;
    FileState jsonlState_;

    // TCPセグメント分割対策: 接続ごとの受信バッファ
    struct StreamState {
        std::vector<uint8_t> buf;
        uint32_t nextSeq = 0;
        bool seqValid = false;
    };
    std::unordered_map<uint32_t, StreamState> streams_;

    // 当日CSVに出力済みの出品シリアル（再取得時の二重追記防止）
    std::unordered_set<uint32_t> csvSeenSerials_;

    void ProcessSubPacket(const uint8_t* data, int len);
    bool ParseRecord(const uint8_t* rec, Record& out) const;

    void SaveCsv(const Record& rec, const SYSTEMTIME& st);
    void SaveJsonl(const Record& rec, const SYSTEMTIME& st, const std::wstring& timestamp);

    void SetupCsvFile(const SYSTEMTIME& st);
    void LoadCsvSerials(const std::wstring& path, std::unordered_set<uint32_t>& out);
    void SetupFile(FileState& state, int year, int month, int day);
    std::wstring GetFilePath(const FileState& state, int year, int month, int day, int index) const;
    int FindLatestFileIndex(const FileState& state, int year, int month, int day) const;
    int CountLinesInFile(const std::wstring& path) const;
};
