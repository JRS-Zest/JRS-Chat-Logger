#pragma once
#include <cstdint>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <vector>
#include <chrono>

// C# InvFullWorker.cs の移植
// キャプチャスレッドをブロックしない専用ワーカー

struct RawPacketItem {
    std::vector<uint8_t> payload;
    bool isIncoming;
    int srcPort;
    int dstPort;
};

class InvFullWorker {
public:
    using InvFullCallback = std::function<void()>;
    using PlaySoundFunc = std::function<void()>;

    InvFullWorker(PlaySoundFunc playFunc, int cooldownMs = 2000);
    ~InvFullWorker();

    void SetInvFullCallback(InvFullCallback cb) { invFullCallback_ = std::move(cb); }
    void SetSoundEnabled(bool enabled) { soundEnabled_ = enabled; }

    // キャプチャスレッドから呼ばれる（即キューに入れて返す）
    void OnRawPacketReceived(const uint8_t* payload, int len, bool isIncoming, int srcPort, int dstPort);

    void Stop();

private:
    static constexpr uint16_t INVENTORY_FULL_SC = 70;

    void WorkerLoop();
    void HandleInventoryFull();

    PlaySoundFunc playFunc_;
    InvFullCallback invFullCallback_;
    int cooldownMs_;
    std::atomic<bool> soundEnabled_{true};
    std::atomic<int> invCooldown_{0};

    std::queue<RawPacketItem> queue_;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::atomic<bool> running_{true};
    std::thread workerThread_;
};
