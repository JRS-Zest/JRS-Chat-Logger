// equipment_packet_processor.h — 0x123D (PlayerInfo) 装備スロット解析
#pragma once

#include "item_dat_loader.h"
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

struct EquipSlotInfo {
    int      slotIndex = -1;       // 0-17
    std::wstring slotName;         // "左武器", "盾", ...
    uint32_t uniqueId = 0;
    uint16_t itemIndex = 0;
    uint8_t  number = 0;
    uint8_t  endurance = 0;
    uint16_t values = 0;
    uint16_t opIndex[3] = {};
    uint8_t  opVal1[3] = {};
    uint8_t  opVal2[3] = {};
    std::wstring itemName;         // item.dat 解決済み
    std::wstring opName[3];        // option.dat 解決済み
    std::wstring opValStr[3];      // "[v1][v2]" 形式 (0除外)
    bool     isEmpty = true;
};

struct EquipmentData {
    std::vector<EquipSlotInfo> slots;  // 常に18要素
};

class EquipmentPacketProcessor {
public:
    using EquipCallback = std::function<void(const EquipmentData&)>;

    explicit EquipmentPacketProcessor(ItemDatLoader& loader);

    void SetEquipCallback(EquipCallback cb) { equipCallback_ = std::move(cb); }

    // パケットを走査し、0x1128 外枠 → 0x123D 内枠を検出・解析
    void ProcessPacket(const uint8_t* payload, int len, bool isIncoming, int srcPort, int dstPort);

private:
    static constexpr int EQUIP_COUNT = 18;
    static constexpr int ITEM_RECORD_SIZE = 30;  // 実サーバ: 4+2+1+1+2+12+8=30

    static const wchar_t* SlotName(int index);
    static std::wstring FormatOpVal(uint8_t v1, uint8_t v2);

    void ParseEquipment(const uint8_t* appData, int appLen);

    ItemDatLoader& loader_;
    EquipCallback equipCallback_;
};
