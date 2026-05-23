#include "inv_full_worker.h"
#include <cstring>

// C# InvFullWorker.cs の完全移植

InvFullWorker::InvFullWorker(PlaySoundFunc playFunc, int cooldownMs)
    : playFunc_(std::move(playFunc)), cooldownMs_(cooldownMs)
{
    workerThread_ = std::thread(&InvFullWorker::WorkerLoop, this);
}

InvFullWorker::~InvFullWorker() {
    Stop();
}

void InvFullWorker::Stop() {
    running_ = false;
    queueCv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void InvFullWorker::OnRawPacketReceived(const uint8_t* payload, int len, bool isIncoming, int srcPort, int dstPort) {
    if (!payload || len <= 0) return;
    RawPacketItem item;
    item.payload.assign(payload, payload + len);
    item.isIncoming = isIncoming;
    item.srcPort = srcPort;
    item.dstPort = dstPort;
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        queue_.push(std::move(item));
    }
    queueCv_.notify_one();
}

void InvFullWorker::WorkerLoop() {
    while (running_) {
        RawPacketItem item;
        {
            std::unique_lock<std::mutex> lk(queueMutex_);
            queueCv_.wait(lk, [this]{ return !queue_.empty() || !running_; });
            if (!running_) break;
            if (queue_.empty()) continue;
            item = std::move(queue_.front());
            queue_.pop();
        }

        try {
            // サーバーからの受信のみ
            if (!item.isIncoming) continue;

            const auto& data = item.payload;
            int len = static_cast<int>(data.size());

            // 単体パケット判定
            if (len >= 20) {
                uint16_t cmdId = *reinterpret_cast<const uint16_t*>(data.data() + 2);
                uint16_t subCmd = *reinterpret_cast<const uint16_t*>(data.data() + 14);
                if (cmdId == 0x1128 && subCmd == 0x1138) {
                    uint16_t statusCode = *reinterpret_cast<const uint16_t*>(data.data() + 18);
                    if (statusCode == INVENTORY_FULL_SC) {
                        HandleInventoryFull();
                        continue;
                    }
                }
            }

            // 結合パケットスキャン
            if (len >= 30) {
                for (int i = 12; i < len - 6; i++) {
                    if (data[i] == 0x38 && data[i + 1] == 0x11) {
                        uint16_t statusCode = 0;
                        if (i + 4 < len) {
                            statusCode = *reinterpret_cast<const uint16_t*>(data.data() + i + 4);
                        }
                        if (statusCode == INVENTORY_FULL_SC) {
                            HandleInventoryFull();
                            break;
                        }
                    }
                }
            }
        } catch (...) {
            // ワーカーは生き続ける
        }
    }
}

void InvFullWorker::HandleInventoryFull() {
    // クールダウンチェック (CAS)
    int expected = 0;
    if (!invCooldown_.compare_exchange_strong(expected, 1)) return;

    try {
        // サウンド再生
        if (soundEnabled_ && playFunc_) {
            try { playFunc_(); } catch (...) {}
        }

        // コールバック
        if (invFullCallback_) {
            try { invFullCallback_(); } catch (...) {}
        }
    } catch (...) {}

    // 非同期でクールダウンリセット
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(cooldownMs_));
        invCooldown_ = 0;
    }).detach();
}
