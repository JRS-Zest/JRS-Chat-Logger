#include "chat_packet_processor.h"
#include <windows.h>
#include <cstring>
#include <algorithm>

// Local SJIS-only decoder (restore original behavior)
static std::wstring SjisToWide(const uint8_t* data, int len) {
    if (!data || len <= 0) return std::wstring();
    int wlen = MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(data), len, nullptr, 0);
    if (wlen <= 0) return std::wstring();
    std::wstring ws(wlen, L'\0');
    MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(data), len, ws.data(), wlen);
    while (!ws.empty() && (ws.back() == L' ' || ws.back() == L'\0')) ws.pop_back();
    return ws;
}

// Markers (C# SubCommandMarker / WhisperTxMarker)
static const uint8_t SubCommandMarker[] = { 0x58, 0x11 };
static const uint8_t WhisperTxMarker[] = { 0x76, 0x11 };

void ChatPacketProcessor::ProcessPacket(const uint8_t* payload, int payloadLen, int /*dstPort*/, bool isIncoming) {
    if (payloadLen < 10) return;
    ProcessChatPackets(payload, payloadLen);
    if (isIncoming) {
        ProcessWhisperTxPacket(payload, payloadLen);
    }
}

void ChatPacketProcessor::ProcessChatPackets(const uint8_t* payload, int len) {
    int idx = 0;
    while (true) {
        idx = FindSequence(payload, len, SubCommandMarker, 2, idx);
        if (idx == -1) break;

        // ServerAll: 0x58 0x11 + 6 bytes all 0xCC
        int saStart = idx + 2;
        int saEnd = saStart + 6;
        bool isServerAll = false;
        if (saEnd <= len) {
            isServerAll = payload[saStart]     == 0xCC &&
                          payload[saStart + 1] == 0xCC &&
                          payload[saStart + 2] == 0xCC &&
                          payload[saStart + 3] == 0xCC &&
                          payload[saStart + 4] == 0xCC &&
                          payload[saStart + 5] == 0xCC;
        }

        if (isServerAll) {
            ParseResult result;
            if (ParseServerAllPacket(payload, len, idx, result)) {
                int hexLen = (std::min)(100, len - idx);
                if (!IsDuplicate(payload, idx, hexLen)) {
                    ChatMessage msg;
                    msg.timestamp = std::chrono::system_clock::now();
                    msg.chatType = ChatType::ServerAll;
                    msg.senderName = result.playerName;
                    msg.message = result.message;
                    if (callback_) callback_(msg);
                }
                idx += (result.consumed > 0) ? result.consumed : 1;
            } else {
                idx++;
            }
            continue;
        }

        // Bitfield route: require 4 CC bytes after marker
        int ccStart = idx + 2;
        int ccEnd = ccStart + 4;
        if (ccEnd <= len) {
            bool isCcSeq = payload[ccStart]     == 0xCC &&
                           payload[ccStart + 1] == 0xCC &&
                           payload[ccStart + 2] == 0xCC &&
                           payload[ccStart + 3] == 0xCC;
            if (!isCcSeq) { idx++; continue; }
        } else {
            idx++; continue;
        }

        int bfStart = idx + 6;
        int bfEnd = bfStart + 4;
        if (bfEnd > len) { idx++; continue; }

        // Extract ChatType from bitfield
        int chatTypeVal = ExtractBitsLsb(payload + bfStart, 4, 22, 5);

        // Validate
        bool valid = false;
        switch (static_cast<ChatType>(static_cast<uint8_t>(chatTypeVal))) {
            case ChatType::General:
            case ChatType::WhisperRx:
            case ChatType::Party:
            case ChatType::Guild:
            case ChatType::All:
                valid = true;
                break;
            default:
                break;
        }
        if (!valid) { idx++; continue; }

        auto chatType = static_cast<ChatType>(static_cast<uint8_t>(chatTypeVal));

        ParseResult result;
        if (ParseChatPacket(payload, len, idx, result)) {
            int hexLen = (std::min)(100, len - idx);
            if (!IsDuplicate(payload, idx, hexLen)) {
                ChatMessage msg;
                msg.timestamp = std::chrono::system_clock::now();
                msg.chatType = chatType;
                msg.senderName = result.playerName;
                msg.message = result.message;
                if (callback_) callback_(msg);
            }
            idx += (result.consumed > 0) ? result.consumed : 1;
        } else {
            idx++;
        }
    }
}

void ChatPacketProcessor::ProcessWhisperTxPacket(const uint8_t* payload, int len) {
    if (len < 30) return;

    int idx = 0;
    while (true) {
        idx = FindSequence(payload, len, WhisperTxMarker, 2, idx);
        if (idx == -1) break;

        // Strict: 4 bytes of 0x00 after marker
        int zStart = idx + 2;
        int zEnd = zStart + 4;
        if (zEnd <= len) {
            if (payload[zStart] != 0 || payload[zStart+1] != 0 || payload[zStart+2] != 0 || payload[zStart+3] != 0) {
                idx++; continue;
            }
        } else {
            idx++; continue;
        }

        // Inner length at idx-2
        int lenOffset = idx - 2;
        if (lenOffset < 0 || lenOffset + 2 > len) { idx++; continue; }

        uint16_t innerLen = *reinterpret_cast<const uint16_t*>(payload + lenOffset);
        if (len < lenOffset + innerLen) { idx++; continue; }

        // Duplicate check
        int hexLen = (std::min)(100, len - idx);
        if (IsDuplicate(payload, idx, hexLen)) { idx++; continue; }

        // Fixed format: [len@-2][2b marker][4b unknown][name:18B][message:...]
        int nameStart = idx + 6;
        const int nameLen = 18;
        int msgStart = nameStart + nameLen;
        if (msgStart > len) { idx++; continue; }

        // Extract name
        const uint8_t* nameField = payload + nameStart;
        int nulPos = -1;
        for (int i = 0; i < nameLen; i++) {
            if (nameField[i] == 0) { nulPos = i; break; }
        }
        int nameActualLen = (nulPos >= 0) ? nulPos : nameLen;
        auto playerName = SjisToWide(nameField, nameActualLen);

        // Extract message
        int innerEnd = lenOffset + innerLen;
        if (innerEnd <= msgStart) { idx++; continue; }

        const uint8_t* msgRegion = payload + msgStart;
        int msgRegionLen = innerEnd - msgStart;
        int msgNulPos = -1;
        for (int i = 0; i < msgRegionLen; i++) {
            if (msgRegion[i] == 0) { msgNulPos = i; break; }
        }
        int msgActualLen = (msgNulPos >= 0) ? msgNulPos : msgRegionLen;
        auto message = SjisToWide(msgRegion, msgActualLen);

        if (!playerName.empty() && !message.empty()) {
            ChatMessage msg;
            msg.timestamp = std::chrono::system_clock::now();
            msg.chatType = ChatType::WhisperTx;
            msg.senderName = playerName;
            msg.message = message;
            if (callback_) callback_(msg);
        }

        idx++;
    }
}

bool ChatPacketProcessor::ParseChatPacket(const uint8_t* data, int dataLen, int cmdIdx, ParseResult& out) {
    // SubCommand(2) + 8 byte header + name\0 + message\0
    int textStart = cmdIdx + 10;
    if (textStart >= dataLen) return false;

    // Player name (null terminated)
    int nullName = -1;
    for (int i = textStart; i < dataLen; i++) {
        if (data[i] == 0) { nullName = i; break; }
    }
    if (nullName == -1) return false;

    int nameLen = nullName - textStart;
    auto playerName = SjisToWide(data + textStart, nameLen);

    // Message (null terminated)
    int msgStart = nullName + 1;
    int nullMsg = -1;
    for (int i = msgStart; i < dataLen; i++) {
        if (data[i] == 0) { nullMsg = i; break; }
    }
    if (nullMsg == -1) nullMsg = dataLen;

    int msgLen = nullMsg - msgStart;
    auto message = SjisToWide(data + msgStart, msgLen);

    if (playerName.empty() || message.empty()) return false;

    out.playerName = playerName;
    out.message = message;
    out.consumed = nullMsg - cmdIdx + 1;
    return true;
}

bool ChatPacketProcessor::ParseServerAllPacket(const uint8_t* data, int dataLen, int cmdIdx, ParseResult& out) {
    // SubCommand(2) + cc pattern(6) + unknown(2) + name\0 + 0x20 + message\0
    int textStart = cmdIdx + 10;
    if (textStart >= dataLen) return false;

    // Player name (null terminated)
    int nullName = -1;
    for (int i = textStart; i < dataLen; i++) {
        if (data[i] == 0) { nullName = i; break; }
    }
    if (nullName == -1) return false;

    int nameLen = nullName - textStart;
    auto playerName = SjisToWide(data + textStart, nameLen);

    // Message: skip leading 0x20
    int msgStart = nullName + 1;
    if (msgStart < dataLen && data[msgStart] == 0x20) msgStart++;

    int nullMsg = -1;
    for (int i = msgStart; i < dataLen; i++) {
        if (data[i] == 0) { nullMsg = i; break; }
    }
    if (nullMsg == -1) nullMsg = dataLen;

    int msgLen = nullMsg - msgStart;
    auto message = SjisToWide(data + msgStart, msgLen);

    if (playerName.empty() || message.empty()) return false;

    out.playerName = playerName;
    out.message = message;
    out.consumed = nullMsg - cmdIdx + 1;
    return true;
}

int ChatPacketProcessor::FindSequence(const uint8_t* data, int dataLen, const uint8_t* seq, int seqLen, int startIdx) {
    if (startIdx < 0 || startIdx >= dataLen || seqLen == 0) return -1;
    for (int i = startIdx; i <= dataLen - seqLen; i++) {
        bool found = true;
        for (int j = 0; j < seqLen; j++) {
            if (data[i + j] != seq[j]) { found = false; break; }
        }
        if (found) return i;
    }
    return -1;
}

int ChatPacketProcessor::ExtractBitsLsb(const uint8_t* bytes, int byteCount, int startBit, int length) {
    int64_t total = 0;
    for (int i = 0; i < byteCount; i++) {
        total |= static_cast<int64_t>(bytes[i]) << (i * 8);
    }
    int64_t mask = (1LL << length) - 1;
    return static_cast<int>((total >> startBit) & mask);
}

bool ChatPacketProcessor::IsDuplicate(const uint8_t* data, int offset, int len) {
    auto now = std::chrono::steady_clock::now();

    // Cleanup old entries
    auto threshold = std::chrono::duration<double>(CLEANUP_THRESHOLD_SEC);
    cache_.erase(
        std::remove_if(cache_.begin(), cache_.end(), [&](const CacheEntry& e) {
            return std::chrono::duration<double>(now - e.time).count() > CLEANUP_THRESHOLD_SEC;
        }),
        cache_.end()
    );

    // Check for duplicate
    std::vector<uint8_t> key(data + offset, data + offset + len);
    for (auto& e : cache_) {
        if (e.data == key) {
            auto elapsed = std::chrono::duration<double>(now - e.time).count();
            if (elapsed < DUPLICATE_THRESHOLD_SEC) return true;
            e.time = now;
            return false;
        }
    }

    // Add new entry
    cache_.push_back({key, now});
    return false;
}
