// gold_processor.h — GoldProcessor: 収入タブのパケット解析
// C# GoldProcessor.cs の C++ 移植
#pragma once

#include <cstdint>
#include <string>
#include <mutex>
#include <atomic>
#include <functional>

class AppSettings;

// UI更新用データ構造
struct GoldStats {
    int64_t pickupGoldTotal = 0;
    int64_t merchantGoldTotal = 0;
    int64_t mobGoldTotal = 0;
    int     mobKillCount = 0;
    int     decomposeCount = 0;
    int     crystalStoneCount = 0;
    int     umuCoinCount = 0;
};

class GoldProcessor {
public:
    GoldProcessor();

    void SetSettings(AppSettings* settings) { settings_ = settings; }

    // パケット処理（キャプチャスレッドから呼ばれる）
    void ProcessPacket(const uint8_t* data, int len, bool isIncoming, int srcPort, int dstPort);

    // 集計開始/停止/リセット
    void StartCollection();
    void StopCollection();
    void ResetCollection();

    // ログ保存
    void SaveIncomeLog();

    // 現在の集計値を取得
    GoldStats GetStats() const;

    bool IsCollecting() const { return collecting_.load(); }

    // UI更新フラグ (ポーリング用)
    bool ConsumeUpdatePending() { return guiUpdatePending_.exchange(0) != 0; }

    // UI更新コールバック (PostMessage用)
    using UpdateCallback = std::function<void()>;
    void SetUpdateCallback(UpdateCallback cb) { updateCallback_ = std::move(cb); }

private:
    static constexpr uint16_t CMD_ID = 0x1128;
    static constexpr uint16_t SUB_CMD_PICKUP = 0x1138;

    mutable std::mutex lock_;
    std::atomic<bool> collecting_{true};
    std::atomic<int> guiUpdatePending_{0};

    int64_t pickupGoldTotal_ = 0;
    int64_t merchantGoldTotal_ = 0;
    int64_t mobGoldTotal_ = 0;
    int     mobKillCount_ = 0;
    int     decomposeCount_ = 0;
    int     tatisFragmentCount_ = 0;
    int     mysteryFragmentCount_ = 0;
    int     crystalStoneCount_ = 0;
    int     adventurerCoinCount_ = 0;
    int     umuCoinCount_ = 0;

    // 重複排除
    std::string lastHash_;
    uint64_t    lastTimeMs_ = 0;
    std::mutex  dupLock_;

    AppSettings* settings_ = nullptr;
    UpdateCallback updateCallback_;

    // パケット解析サブルーチン
    bool IsCombinedPickupGoldPacket(const uint8_t* data, int len);
    void ProcessCombinedPickupGoldPacket(const uint8_t* data, int len);
    void AnalyzePickupGoldPacket(const uint8_t* data, int len);
    void ProcessCombinedMerchantSalesPacket(const uint8_t* data, int len);
    bool IsMobStatsPacket(const uint8_t* data, int len);
    void ProcessMobStatsPacket(const uint8_t* data, int len);
    void ProcessDecomposePacket(const uint8_t* data, int len);

    // ヘルパー
    static int IndexOf(const uint8_t* haystack, int hayLen, const uint8_t* needle, int needleLen, int start);
    static std::string ComputeMD5(const uint8_t* data, int len);
    static uint64_t NowMs();
    void NotifyUpdate();
};
