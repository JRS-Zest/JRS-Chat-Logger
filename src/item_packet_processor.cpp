#include "item_packet_processor.h"
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

// C# ItemPacketProcessor.cs の完全移植

static const uint16_t INVENTORY_FULL_SC = 70;
static const int MAX_LOG_OPTIONS = 3;

ItemPacketProcessor::ItemPacketProcessor(ItemDatLoader& loader, WantedRegexService& regex)
    : loader_(loader), regex_(regex)
{
}

// ペイロードバイト列を16進文字列に変換（重複チェック用）
static std::string ToHexString(const uint8_t* data, int len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < len; i++) {
        oss << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return oss.str();
}

bool ItemPacketProcessor::IsItemPacket(const uint8_t* data, int len) const {
    if (len < 20) return false;
    uint16_t cmdId = *reinterpret_cast<const uint16_t*>(data + 2);
    uint16_t subCmd = *reinterpret_cast<const uint16_t*>(data + 14);
    return cmdId == 0x1128 && subCmd == 0x1138;
}

bool ItemPacketProcessor::IsCombinedItemPacket(const uint8_t* data, int len) const {
    if (len < 20 || len > 500) return false;
    uint16_t cmdId = *reinterpret_cast<const uint16_t*>(data + 2);
    if (cmdId != 0x1128) return false;
    // 0x1138 を探す
    for (int i = 12; i < len - 4; i++) {
        if (data[i] == 0x38 && data[i + 1] == 0x11) {
            return true;
        }
    }
    return false;
}

bool ItemPacketProcessor::IsDuplicatePacket(const uint8_t* data, int len) {
    auto now = std::chrono::steady_clock::now();
    auto hash = ToHexString(data, len);
    auto it = lastPacketTime_.find(hash);
    if (it != lastPacketTime_.end()) {
        auto elapsed = std::chrono::duration<double>(now - it->second).count();
        if (elapsed < 0.01) {
            return true;
        }
    }
    lastPacketTime_[hash] = now;
    // 60秒以上経ったキャッシュを削除（Cleanup相当）
    static int cleanupCounter = 0;
    if (++cleanupCounter >= 100) {
        cleanupCounter = 0;
        for (auto it2 = lastPacketTime_.begin(); it2 != lastPacketTime_.end(); ) {
            auto elapsed2 = std::chrono::duration<double>(now - it2->second).count();
            if (elapsed2 > 60.0) {
                it2 = lastPacketTime_.erase(it2);
            } else {
                ++it2;
            }
        }
    }
    return false;
}

void ItemPacketProcessor::ProcessPacket(const uint8_t* payload, int len, bool isIncoming, int /*srcPort*/, int /*dstPort*/) {
    // サーバーからの受信パケットのみ処理
    if (!isIncoming) return;

    if (IsItemPacket(payload, len)) {
        AnalyzeItemPacket(payload, len);
        return;
    }
    if (IsCombinedItemPacket(payload, len)) {
        AnalyzeCombinedItemPacket(payload, len);
    }
}

void ItemPacketProcessor::AnalyzeItemPacket(const uint8_t* data, int len) {
    // 重複チェック
    if (IsDuplicatePacket(data, len)) return;

    try {
        // ステータスコード
        uint16_t statusCode = 0;
        if (len >= 20) {
            statusCode = *reinterpret_cast<const uint16_t*>(data + 18);
        }

        // アイテムシリアル
        if (len < 26) return;
        uint16_t itemSerial = *reinterpret_cast<const uint16_t*>(data + 24);

        // 無効なシリアルをスキップ
        if (itemSerial == 52428 || itemSerial == 0) return;

        // ステータスコードが0以外ならスキップ（inv_fullは専用ワーカーへ移行済み）
        if (statusCode != 0) return;

        auto itemName = loader_.GetItemName(itemSerial);
        std::vector<std::wstring> options;

        // オプション読み取り（固定位置: 30, 34, 38）
        int optionPositions[] = { 30, 34, 38 };
        for (int pos : optionPositions) {
            if (pos + 2 > len) break;
            uint16_t optionId = *reinterpret_cast<const uint16_t*>(data + pos);
            if (optionId == 0xFFFF) continue;
            auto optionName = loader_.GetOptionName(optionId);
            if (!optionName.empty()) {
                options.push_back(optionName);
            }
        }

        // 通知判定
        auto matchResult = regex_.GetMatchingCells(itemName, options);
        bool isNotification = matchResult.itemMatch;
        for (auto f : matchResult.optionMatches) {
            if (f) { isNotification = true; break; }
        }

        ItemPickupInfo info;
        info.timestamp = std::chrono::system_clock::now();
        info.itemSerial = itemSerial;
        info.itemName = itemName;
        info.options = options;
        info.isNotificationTarget = isNotification;
        info.matchedByName = matchResult.itemMatch;
        bool matchedByOpt = false;
        for (auto f : matchResult.optionMatches) { if (f) { matchedByOpt = true; break; } }
        info.matchedByOption = matchedByOpt;

        // 再生はプロセッサ側で先行して行う（UIスレッドに渡す前に）
        if (isNotification && soundFunc_) {
            if (matchResult.itemMatch) {
                soundFunc_(true); // item sound
            } else if (matchedByOpt) {
                soundFunc_(false); // option sound
            } else {
                soundFunc_(true); // item sound (fallback)
            }
        }

        // コールバック
        if (itemCallback_) {
            itemCallback_(info);
        }

    } catch (...) {
        // 解析エラー
    }
}

void ItemPacketProcessor::AnalyzeCombinedItemPacket(const uint8_t* data, int len) {
    // 重複チェック
    if (IsDuplicatePacket(data, len)) return;

    try {
        for (int i = 12; i < len - 30; i++) {
            if (data[i] == 0x38 && data[i + 1] == 0x11) {
                // ステータスコード
                uint16_t statusCode = 0;
                if (i + 6 < len) {
                    statusCode = *reinterpret_cast<const uint16_t*>(data + i + 4);
                }

                // アイテムシリアル
                if (i + 12 > len) continue;
                uint16_t itemSerial = *reinterpret_cast<const uint16_t*>(data + i + 10);

                // 無効なシリアルをスキップ
                if (itemSerial == 52428 || itemSerial == 0) continue;

                // inv_fullは専用ワーカーへ移行済み
                if (statusCode != 0) continue;

                auto itemName = loader_.GetItemName(itemSerial);
                std::vector<std::wstring> options;

                // オプション読み取り
                int currentPos = i + 16;
                int count = 0;
                while (currentPos + 2 <= len && count < MAX_LOG_OPTIONS) {
                    uint16_t optionId = *reinterpret_cast<const uint16_t*>(data + currentPos);
                    if (optionId == 0xFFFF) break;
                    auto optionName = loader_.GetOptionName(optionId);
                    if (!optionName.empty()) {
                        options.push_back(optionName);
                    }
                    currentPos += 4; // 2バイトID + 2バイト値
                    count++;
                }

                // 通知判定
                auto matchResult = regex_.GetMatchingCells(itemName, options);
                bool isNotification = matchResult.itemMatch;
                for (auto f : matchResult.optionMatches) {
                    if (f) { isNotification = true; break; }
                }

                ItemPickupInfo info;
                info.timestamp = std::chrono::system_clock::now();
                info.itemSerial = itemSerial;
                info.itemName = itemName;
                info.options = options;
                info.isNotificationTarget = isNotification;
                info.matchedByName = matchResult.itemMatch;
                bool matchedByOpt = false;
                for (auto f : matchResult.optionMatches) { if (f) { matchedByOpt = true; break; } }
                info.matchedByOption = matchedByOpt;

                // 再生はプロセッサ側で先行して行う
                if (isNotification && soundFunc_) {
                    if (matchResult.itemMatch) {
                        soundFunc_(true);
                    } else if (matchedByOpt) {
                        soundFunc_(false);
                    } else {
                        soundFunc_(true);
                    }
                }

                if (itemCallback_) {
                    itemCallback_(info);
                }

                break; // 1つ見つけたら終了
            }
        }
    } catch (...) {
        // 解析エラー
    }
}

void ItemPacketProcessor::ClearCache() {
    lastPacketTime_.clear();
}
