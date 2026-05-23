// equipment_packet_processor.cpp — 0x123D (PlayerInfo) 装備スロット解析
#include "equipment_packet_processor.h"
#include <cstring>

static inline uint16_t ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
static inline uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

EquipmentPacketProcessor::EquipmentPacketProcessor(ItemDatLoader& loader)
    : loader_(loader)
{
}

const wchar_t* EquipmentPacketProcessor::SlotName(int index) {
    static const wchar_t* names[EQUIP_COUNT] = {
        L"左武器", L"補助",  L"鎧",   L"手",   L"頭",
        L"背/耳",  L"首",   L"腰",   L"足",
        L"指1",    L"指2",  L"指3",  L"指4",
        L"指5",    L"指6",  L"指7",  L"指8",
        L"右武器"
    };
    if (index >= 0 && index < EQUIP_COUNT) return names[index];
    return L"?";
}

std::wstring EquipmentPacketProcessor::FormatOpVal(uint8_t v1, uint8_t v2) {
    if (v1 == 0 && v2 == 0) return {};
    std::wstring s;
    if (v1 != 0) { s += L"["; s += std::to_wstring(v1); s += L"]"; }
    if (v2 != 0) { s += L"["; s += std::to_wstring(v2); s += L"]"; }
    return s;
}

void EquipmentPacketProcessor::ProcessPacket(const uint8_t* payload, int len, bool isIncoming, int /*srcPort*/, int /*dstPort*/) {
    if (!isIncoming) return;
    if (len < 20) return;

    // 0x123D を直接走査 (結合パケット対応: 0x1128 外枠に依存しない)
    for (int i = 0; i <= len - 8; i++) {
        uint16_t innerLen = ReadU16(payload + i);
        if (innerLen < 8 || innerLen > 4096) continue;
        uint16_t innerCmd = ReadU16(payload + i + 2);
        if (innerCmd != 0x123D) continue;

        int appStart = i + 6;
        int appAvail = len - appStart;
        int appExpected = innerLen - 6;
        int appLen = (appAvail < appExpected) ? appAvail : appExpected;

        // 装備データの必要最低限: junk(2) + header(90) + 18*30 = 632
        if (appLen >= 2 + 90 + EQUIP_COUNT * ITEM_RECORD_SIZE) {
            ParseEquipment(payload + appStart, appLen);
            return;  // 装備データは1キャラ分でOK
        }
    }
}

void EquipmentPacketProcessor::ParseEquipment(const uint8_t* appData, int appLen) {
    int pos = 2;  // skip 2-byte junk

    // ヘッダフィールド読み飛ばし (C#/Python と同一オフセット)
    // Level(4) + EXP(4) + SP(4) + NowHP(4) + BaseHP(4) = 20
    pos += 20;
    // NowCP(4) + BaseCP(4) = 8
    pos += 8;
    // StateHPCPBonus(2) + LevelHPCPBobuns(2) = 4
    pos += 4;
    // ActorStatus (7 × 2) = 14
    pos += 14;
    // unknown_0(2) + MaxPower(2) + MinPower(2) + Defence(2) + Tendency(2) = 10
    pos += 10;
    // Magic (6 × 2) = 12
    pos += 12;
    // StatusAbnormal (9 × 2) = 18
    pos += 18;
    // AllStatus* (3 × 2) = 6
    pos += 6;

    // pos = 2 + 20 + 8 + 4 + 14 + 10 + 12 + 18 + 6 = 94
    // ここから装備アイテム配列

    if (pos + EQUIP_COUNT * ITEM_RECORD_SIZE > appLen) return;

    EquipmentData data;
    data.slots.resize(EQUIP_COUNT);

    for (int i = 0; i < EQUIP_COUNT; i++) {
        auto& slot = data.slots[i];
        slot.slotIndex = i;
        slot.slotName = SlotName(i);

        const uint8_t* rec = appData + pos;
        slot.uniqueId  = ReadU32(rec + 0);
        slot.itemIndex = ReadU16(rec + 4);
        slot.number    = rec[6];
        slot.endurance = rec[7];
        slot.values    = ReadU16(rec + 8);

        for (int oi = 0; oi < 3; oi++) {
            int opOff = 10 + oi * 4;
            slot.opIndex[oi] = ReadU16(rec + opOff);
            slot.opVal1[oi]  = rec[opOff + 2];
            slot.opVal2[oi]  = rec[opOff + 3];
        }
        // flags: 8 bytes at offset 22 (skip)

        slot.isEmpty = (slot.itemIndex == 0 || slot.itemIndex == 0xFFFF);

        // 名前解決
        if (!slot.isEmpty) {
            slot.itemName = loader_.GetItemName(slot.itemIndex);
            if (slot.itemName.empty()) {
                wchar_t buf[32];
                swprintf_s(buf, L"0x%04X", slot.itemIndex);
                slot.itemName = buf;
            }
        }

        for (int oi = 0; oi < 3; oi++) {
            if (slot.opIndex[oi] != 0 && slot.opIndex[oi] != 0xFFFF) {
                slot.opName[oi] = loader_.GetOptionName(slot.opIndex[oi]);
                if (slot.opName[oi].empty()) {
                    wchar_t buf[32];
                    swprintf_s(buf, L"0x%04X", slot.opIndex[oi]);
                    slot.opName[oi] = buf;
                }
                slot.opValStr[oi] = FormatOpVal(slot.opVal1[oi], slot.opVal2[oi]);
            }
        }

        pos += ITEM_RECORD_SIZE;
    }

    if (equipCallback_) {
        equipCallback_(data);
    }
}
