// drop_packet_processor.cpp — DroppingItems (0x1137) パケット解析
#include "drop_packet_processor.h"
#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

DropPacketProcessor::DropPacketProcessor(ItemDatLoader& loader)
    : loader_(loader)
{
}

void DropPacketProcessor::LoadDropDcDat(const std::wstring& exeDir, const std::wstring& folderName) {
    dcLoaded_ = false;
    dcBytes_.clear();
    blockOrderIds_.clear();
    auto path = (fs::path(exeDir) / L"DC" / folderName / L"DC.dat").wstring();
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return;
    dcBytes_ = std::vector<uint8_t>(std::istreambuf_iterator<char>(ifs), {});

    // item.dat のブロック順を取得してマッピング構築
    blockOrderIds_.clear();
    auto blocks = loader_.GetAllBlocksOrdered();
    for (auto& b : blocks) {
        blockOrderIds_.push_back(b.id);
    }
    dcLoaded_ = true;
}

bool DropPacketProcessor::IsNotificationTarget(uint16_t itemId) const {
    if (!dcLoaded_ || dcBytes_.empty()) return false;

    // blockOrderIds_ のインデックスが DC.dat のバイト位置に対応
    for (size_t i = 0; i < blockOrderIds_.size() && i < dcBytes_.size(); i++) {
        if (blockOrderIds_[i] == static_cast<int>(itemId)) {
            return dcBytes_[i] == 0x00;  // ON=0x00
        }
    }
    return false;
}

void DropPacketProcessor::ProcessPacket(const uint8_t* payload, int len, bool isIncoming, int /*srcPort*/, int /*dstPort*/) {
    if (!isIncoming) return;
    if (len < 10) return;

    // 0x1137 マーカー (リトルエンディアン: 37 11 00 00) を走査
    static const uint8_t marker[] = { 0x37, 0x11, 0x00, 0x00 };
    for (int i = 0; i <= len - 4; i++) {
        if (std::memcmp(payload + i, marker, 4) == 0) {
            int remaining = len - i;
            if (remaining >= 6) {  // marker(4) + count(2) 最低限
                AnalyzeDropPacket(payload + i, remaining);
            }
        }
    }
}

void DropPacketProcessor::AnalyzeDropPacket(const uint8_t* data, int remaining) {
    // data[0..3] = 0x37,0x11,0x00,0x00 (マーカー)
    // data[4..5] = count (uint16 LE)
    // data[6..] = DropItem[count] (各10byte)

    uint16_t count = *reinterpret_cast<const uint16_t*>(data + 4);
    if (count == 0 || count > 200) return;  // 異常値ガード

    int needed = 6 + count * 10;
    if (remaining < needed) return;

    auto now = std::chrono::system_clock::now();

    for (uint16_t i = 0; i < count; i++) {
        const uint8_t* item = data + 6 + i * 10;

        // 80bit 整数としてビットフィールド抽出 (リトルエンディアン)
        uint64_t lo = *reinterpret_cast<const uint64_t*>(item);      // bit 0-63
        uint16_t hi = *reinterpret_cast<const uint16_t*>(item + 8);  // bit 64-79

        uint16_t index    = static_cast<uint16_t>(lo & 0x3FF);    lo >>= 10;
        uint16_t itemId   = static_cast<uint16_t>(lo & 0x3FFF);   lo >>= 14;
        uint8_t  itemType = static_cast<uint8_t>(lo & 0xFF);      lo >>= 8;
        uint16_t itemNum  = static_cast<uint16_t>(lo & 0xFFFF);   lo >>= 16;
        uint16_t posX     = static_cast<uint16_t>(lo & 0xFFFF);
        uint16_t posY     = hi;

        DropItemInfo info;
        info.timestamp = now;
        info.index = index;
        info.itemId = itemId;
        info.itemType = itemType;
        info.itemNum = itemNum;
        info.posX = posX;
        info.posY = posY;
        info.isGold = (itemId == 0);

        // アイテム名
        if (info.isGold) {
            info.itemName = L"ゴールド";
        } else {
            info.itemName = loader_.GetItemName(itemId);
        }

        // 座標変換 (タイルサイズ: X/64, Y/32)
        int tileX = posX / 64;
        int tileY = posY / 32;
        wchar_t coordBuf[64];
        swprintf_s(coordBuf, L"%d, %d", tileX, tileY);
        info.coordText = coordBuf;

        // 通知判定
        info.isNotificationTarget = IsNotificationTarget(itemId);

        if (dropCallback_) {
            dropCallback_(info);
        }
    }
}
