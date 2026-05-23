// drop_packet_processor.h — DroppingItems (0x1137) パケット解析
#pragma once

#include "item_dat_loader.h"
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <chrono>

struct DropItemInfo {
    std::chrono::system_clock::time_point timestamp;
    uint16_t index = 0;
    uint16_t itemId = 0;
    uint8_t  itemType = 0;
    uint16_t itemNum = 0;
    uint16_t posX = 0;
    uint16_t posY = 0;
    std::wstring itemName;
    std::wstring coordText;      // "X, Y" (タイル変換後)
    bool isGold = false;
    bool isNotificationTarget = false;
};

class DropPacketProcessor {
public:
    using DropCallback = std::function<void(const DropItemInfo&)>;

    explicit DropPacketProcessor(ItemDatLoader& loader);

    void SetDropCallback(DropCallback cb) { dropCallback_ = std::move(cb); }

    // DC.dat (DC/<folderName>/DC.dat) を読み込み、通知ON/OFFマップを構築
    void LoadDropDcDat(const std::wstring& exeDir, const std::wstring& folderName);
    bool IsDcLoaded() const { return dcLoaded_; }

    // パケットを走査し、0x1137 を検出・解析する
    void ProcessPacket(const uint8_t* payload, int len, bool isIncoming, int srcPort, int dstPort);

    // DC.dat の通知判定 (itemId に対応する位置が ON=0x00 なら true)
    bool IsNotificationTarget(uint16_t itemId) const;

private:
    void AnalyzeDropPacket(const uint8_t* marker, int remaining);

    ItemDatLoader& loader_;
    DropCallback dropCallback_;

    // Drop/Ultimate/DC.dat のバイト列
    std::vector<uint8_t> dcBytes_;
    bool dcLoaded_ = false;

    // item.dat のブロック順 ID→DC.dat位置マッピング
    // dcIndexMap_[itemId] = DC.dat 内のバイトオフセット
    std::vector<int> blockOrderIds_;  // ブロック順 ItemID のリスト
};
