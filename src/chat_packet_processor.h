#pragma once
#include "chat_type.h"
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <chrono>

// C# ChatPacketProcessor.cs の完全移植

struct ChatMessage {
    std::chrono::system_clock::time_point timestamp;
    ChatType chatType;
    std::wstring senderName;
    std::wstring message;
};

class ChatPacketProcessor {
public:
    using ChatCallback = std::function<void(const ChatMessage&)>;

    void SetCallback(ChatCallback cb) { callback_ = std::move(cb); }
    void ProcessPacket(const uint8_t* payload, int payloadLen, int dstPort, bool isIncoming);

private:
    void ProcessChatPackets(const uint8_t* payload, int len);
    void ProcessWhisperTxPacket(const uint8_t* payload, int len);

    struct ParseResult {
        std::wstring playerName;
        std::wstring message;
        int consumed;
    };

    bool ParseChatPacket(const uint8_t* data, int dataLen, int cmdIdx, ParseResult& out);
    bool ParseServerAllPacket(const uint8_t* data, int dataLen, int cmdIdx, ParseResult& out);

    static int FindSequence(const uint8_t* data, int dataLen, const uint8_t* seq, int seqLen, int startIdx);
    static int ExtractBitsLsb(const uint8_t* bytes, int byteCount, int startBit, int length);
    bool IsDuplicate(const uint8_t* data, int offset, int len);
    void CleanupCache();

    ChatCallback callback_;

    // 重複チェックキャッシュ
    struct CacheEntry {
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point time;
    };
    std::vector<CacheEntry> cache_;
    static constexpr double DUPLICATE_THRESHOLD_SEC = 0.5;
    static constexpr double CLEANUP_THRESHOLD_SEC = 5.0;
};
