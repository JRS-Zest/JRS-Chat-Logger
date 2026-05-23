// special_drop_processor.h — Special drops (0x1133) パケット解析（地下界の書のみ）
#pragma once

#include "drop_packet_processor.h"
#include <vector>
#include <cstdint>
#include <functional>

class SpecialDropProcessor {
public:
    using DropCallback = DropPacketProcessor::DropCallback;
    using DebugCallback = std::function<void(const std::wstring&)>;

    explicit SpecialDropProcessor(ItemDatLoader& loader);

    void SetDropCallback(DropCallback cb) { dropCallback_ = std::move(cb); }
    void SetDebugCallback(DebugCallback cb) { debugCallback_ = std::move(cb); }

    void ProcessPacket(const uint8_t* payload, int len, bool isIncoming, int srcPort, int dstPort);

private:
    void AnalyzeAt(const uint8_t* base);

    ItemDatLoader& loader_;
    DropCallback dropCallback_;
    DebugCallback debugCallback_;
    static constexpr uint8_t marker_[6] = { 0x10, 0x00, 0x33, 0x11, 0x00, 0x00 };
};
