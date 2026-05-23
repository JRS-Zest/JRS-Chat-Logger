// special_drop_processor.cpp — Special drops (0x1133) パケット解析（地下界の書のみ）
#include "special_drop_processor.h"
#include <cstring>

constexpr uint8_t SpecialDropProcessor::marker_[];

SpecialDropProcessor::SpecialDropProcessor(ItemDatLoader& loader)
    : loader_(loader)
{
}

void SpecialDropProcessor::ProcessPacket(const uint8_t* payload, int len, bool isIncoming, int /*srcPort*/, int /*dstPort*/) {
    if (!isIncoming) return;
    if (len < 16) return;

    // 6バイトマーカーを走査。検出位置を相対オフセット0とする。
    for (int i = 0; i <= len - 16; i++) {
        if (std::memcmp(payload + i, marker_, 6) == 0) {
            AnalyzeAt(payload + i);
        }
    }
}

void SpecialDropProcessor::AnalyzeAt(const uint8_t* base) {
    // base[0..5] = marker
    // 追加フィルタ: base[8..9] が 0x4E 0xC0 のものを SpecialItem 候補とする
    if (base[8] != 0x4E || base[9] != 0xC0) return;

    // デバッグ出力はマーカー＋種別フィルタを通ったものだけ出す
    // （デバッグログはテスト完了のため無効化: 以下は将来のために残す）
    /*
    if (debugCallback_) {
        uint16_t posX_dbg = static_cast<uint16_t>(base[12]) | (static_cast<uint16_t>(base[13]) << 8);
        uint16_t posY_dbg = static_cast<uint16_t>(base[14]) | (static_cast<uint16_t>(base[15]) << 8);
        int tileX_dbg = posX_dbg / 64;
        int tileY_dbg = posY_dbg / 32;

        wchar_t hexbuf[128] = {};
        for (int i = 0; i < 16; ++i) {
            swprintf_s(hexbuf + i * 2, 128 - i * 2, L"%02X", base[i]);
        }
        swprintf_s(hexbuf + 32, 128 - 32, L" （座標 %03d,%03d）", tileX_dbg, tileY_dbg);
        debugCallback_(std::wstring(hexbuf));
    }
    */

    uint8_t id = base[7];
    uint8_t type = (id & 0x0E) >> 1; // ビット3..1 を種別として抽出

    // type -> itemId マッピング（既知のみ割当）
    int mappedItemId = 0;
    switch (type) {
    case 0: mappedItemId = 5044; break; // 精霊の叡智
    case 2: mappedItemId = 5045; break; // 大自然の加護（暫定割当）
    case 4: mappedItemId = 5046; break; // 神霊の吐息
    case 6: mappedItemId = 5047; break; // 地下界の書
    default:
        // 未割当 type (1,3,5,7) はドロップタブ表示しない
        return;
    }

    // 必要なフィールドのみ抽出（offset12-13: X, 14-15: Y）
    uint16_t posX = static_cast<uint16_t>(base[12]) | (static_cast<uint16_t>(base[13]) << 8);
    uint16_t posY = static_cast<uint16_t>(base[14]) | (static_cast<uint16_t>(base[15]) << 8);

    DropItemInfo info;
    info.timestamp = std::chrono::system_clock::now();
    info.index = 0;
    info.itemId = static_cast<uint16_t>(mappedItemId);
    info.itemType = 0;
    info.itemNum = 0;
    info.posX = posX;
    info.posY = posY;
    info.isGold = false;

    // アイテム名は loader による取得
    info.itemName = loader_.GetItemName(info.itemId);

    // 座標変換
    int tileX = posX / 64;
    int tileY = posY / 32;
    wchar_t coordBuf[64];
    swprintf_s(coordBuf, L"%d, %d", tileX, tileY);
    info.coordText = coordBuf;

    // 通知判定は呼び出し元 (main) で行う想定なのでここでは false にしておく
    info.isNotificationTarget = false;

    if (dropCallback_) dropCallback_(info);
}
