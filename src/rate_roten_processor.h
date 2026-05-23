#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

class ItemDatLoader;

struct RateRotenItem {
    int slot = 0;
    std::wstring itemName;
    int count = 0;
    std::vector<std::wstring> options;
    uint32_t price = 0;
    std::wstring currency;
};

struct RateRotenResult {
    std::wstring charName;
    std::vector<RateRotenItem> items;
};

// C# RateRotenProcessor の移植
// パケットから露店価格情報を解析し、JSONL ログ保存 + UI コールバック
class RateRotenProcessor {
public:
    using ResultCallback = std::function<void(const RateRotenResult& result)>;

    explicit RateRotenProcessor(ItemDatLoader& itemDatLoader, int maxLinesPerFile = 1000);

    void SetCallback(ResultCallback cb) { callback_ = std::move(cb); }

    // パケット処理 (キャプチャスレッドから呼ばれる)
    void ProcessPacket(const uint8_t* data, int len, bool isIncoming, int srcPort, int dstPort);

    void ResetState();

private:
    static constexpr uint8_t COMMAND_ID_0 = 0x28;
    static constexpr uint8_t COMMAND_ID_1 = 0x11;
    static constexpr uint8_t SUB_COMMAND_BYTES[6] = {0x0d, 0x12, 0x00, 0x00, 0xCC, 0xCC};

    static constexpr int CHAR_NAME_OFFSET_FROM_SUBCMD = 74;
    static constexpr int CHAR_NAME_FIELD_SIZE = 20;
    static constexpr int SLOT_SIZE = 40;

    ItemDatLoader& itemDatLoader_;
    ResultCallback callback_;

    // JSONL ログ
    std::wstring logDir_;
    std::wstring debugDir_;
    int maxLinesPerFile_;
    int currentDate_[3] = {0, 0, 0}; // year, month, day
    bool dateValid_ = false;
    int currentFileIndex_ = 0;
    int currentLineCount_ = 0;
    std::wstring currentLogFilePath_;

    // パケット解析
    bool ParseFromSubcmd(const uint8_t* data, int len, int subcmdPos,
                         std::wstring& outCharName, std::vector<RateRotenItem>& outItems);
    std::vector<std::wstring> ParseOptions(const uint8_t* optionsBytes, int optLen);
    static int FindSequence(const uint8_t* data, int dataLen, const uint8_t* seq, int seqLen, int start);

    // ログ書き込み
    void SaveItemAsJson(const RateRotenItem& item, const std::wstring& timestamp);
    void SetupJsonLogFile(int year, int month, int day);
    std::wstring GetLogFilePath(int year, int month, int day, int index);
    int FindLatestFileIndex(int year, int month, int day);
    int CountLinesInFile(const std::wstring& path);

    // デバッグ
    void DumpDebugHex(const uint8_t* data, int len, int srcPort, int dstPort, const std::wstring& charName);
};
