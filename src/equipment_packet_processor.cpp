// equipment_packet_processor.cpp — 0x123D (PlayerInfo) 装備スロット解析
#include "equipment_packet_processor.h"
#include <cstring>

// =====================================================================
// 【参考パケット】装備スロット (0x123D / PlayerInfo)
// 取得日時: 2026-09-03（ログイン直後・キャラ「素麺」）
// 備考: アップデートで装備配列の開始位置が変わった。
//       ヘッダ終端に誤って加算されていた「+2バイト」を除去し、
//       装備開始を 128 → 126 に修正した。
// ---------------------------------------------------------------------
// Hex（0x1128 ラッパー Count=3。第1インナー InnerID=0x123D, InnerLen=0x1b25=6949）:
//   e11b2811cdcdcdcd03000000251b3d12000062050000489930221fb30100942c4400d0070000
//   38df0700e803000064006400c70b000019000000da070000050000000a0000000f0000002311
//   00007c0101000100000064000000000000000000000000000000000000000000000000000000
//   00000000000000000000ef0b4a01370c01090000ab020200ae022800b50208017a00000000...
//
// バイト構造（idx=InnerLen位置=12, InnerID 0x123D は offset14-15）:
//   [2B 全体長][2B 0x1128][4B cd cd cd cd][4B Count=3]
//   [2B InnerLen][2B InnerID=0x123D][2B 予約 00 00]
//   [ヘッダ(ステータス類) ... ] → 装備配列は InnerLen位置+114 = offset 126 から開始
//
// 装備レコード（18個 × 66バイト。22バイト実データ + 44バイトパディング）:
//   [0-3] uniqueId(4) [4-5] itemIndex(2) [6] number(1) [7] endurance(1)
//   [8-9] values(2)
//   [10-13] op0(opIndex2+opVal1+opVal2) [14-17] op1 [18-21] op2
//   [22-29] flags(8, スキップ。装備中=7a00 / 空き=7800) [30-65] padding
//
// 判定（ProcessPacket）:
//   - 走査: innerLen(2B, 8〜8192) + innerCmd==0x123D を探す
//   - appStart = i + 44 (i=InnerLen位置)。ParseEquipment で 2(junk)+68(header) を
//     スキップして装備配列を読む（合計70バイトスキップ）
// =====================================================================

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
        if (innerLen < 8 || innerLen > 8192) continue;
        uint16_t innerCmd = ReadU16(payload + i + 2);
        if (innerCmd != 0x123D) continue;

        // 0x123D を見つけた位置(i)から44バイト進んだ場所が装備開始位置
        int appStart = i + 44;

        // 念のため、そこから66バイト×18個の領域がパケット内に収まっているか確認
        if (appStart + (18 * ITEM_RECORD_SIZE) <= len) {
            ParseEquipment(payload + appStart, len - appStart);
            return;  // 装備データは1キャラ分でOK
        }
    }
}

void EquipmentPacketProcessor::ParseEquipment(const uint8_t* appData, int appLen) {
    int pos = 2;  // skip 2-byte junk

    // ヘッダフィールド読み飛ばし (最新パケット構造に合わせた調整: 計70バイト)
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
    // 残りのヘッダ領域 (2バイト追加)
    pos += 2;

    // pos = 2 + 20 + 8 + 4 + 14 + 10 + 12 + 2 = 72
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
