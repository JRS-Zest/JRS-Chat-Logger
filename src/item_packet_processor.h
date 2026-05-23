#pragma once
#include "item_dat_loader.h"
#include "wanted_regex_service.h"
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include <chrono>
#include <unordered_map>

// C# ItemPacketProcessor.cs の移植

struct ItemPickupInfo {
    std::chrono::system_clock::time_point timestamp;
    int itemSerial = 0;
    std::wstring itemName;
    std::vector<std::wstring> options;
    bool isNotificationTarget = false;
    bool matchedByName = false;
    bool matchedByOption = false;
};

class ItemPacketProcessor {
public:
    using ItemCallback = std::function<void(const ItemPickupInfo&)>;
    using InvFullCallback = std::function<void()>;
    using SoundPlayFunc = std::function<void(bool /*isItem*/)>;

    ItemPacketProcessor(ItemDatLoader& loader, WantedRegexService& regex);

    void SetItemCallback(ItemCallback cb) { itemCallback_ = std::move(cb); }
    void SetInvFullCallback(InvFullCallback cb) { invFullCallback_ = std::move(cb); }
    void SetSoundFunc(SoundPlayFunc fn) { soundFunc_ = std::move(fn); }

    void ProcessPacket(const uint8_t* payload, int len, bool isIncoming, int srcPort, int dstPort);
    void ClearCache();

private:
    bool IsItemPacket(const uint8_t* data, int len) const;
    bool IsCombinedItemPacket(const uint8_t* data, int len) const;
    void AnalyzeItemPacket(const uint8_t* data, int len);
    void AnalyzeCombinedItemPacket(const uint8_t* data, int len);
    bool IsDuplicatePacket(const uint8_t* data, int len);

    ItemDatLoader& loader_;
    WantedRegexService& regex_;
    ItemCallback itemCallback_;
    InvFullCallback invFullCallback_;
    SoundPlayFunc soundFunc_;

    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastPacketTime_;
};
