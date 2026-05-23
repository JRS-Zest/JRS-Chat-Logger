#pragma once
#include <string>
#include <cstdint>
// COLORREF forward definition (avoid pulling in windef.h/winsock.h)
#ifndef _WINDEF_
typedef unsigned long COLORREF;
#ifndef RGB
#define RGB(r,g,b) ((COLORREF)(((unsigned char)(r)|((unsigned short)((unsigned char)(g))<<8))|(((unsigned long)(unsigned char)(b))<<16)))
#endif
#endif

// C# ChatType.cs の完全移植

enum class ChatType : uint8_t {
    General   = 0,
    WhisperRx = 1,
    Party     = 2,
    Guild     = 3,
    All       = 4,
    ServerAll = 254,
    WhisperTx = 255,
};

inline const wchar_t* GetChatTypePrefix(ChatType ct) {
    switch (ct) {
        case ChatType::General:   return L"[一般]";
        case ChatType::WhisperRx: return L"[耳受]";
        case ChatType::Party:     return L"[PT]";
        case ChatType::Guild:     return L"[ギルド]";
        case ChatType::All:       return L"[全体]";
        case ChatType::ServerAll: return L"[鯖全]";
        case ChatType::WhisperTx: return L"[耳送]";
        default:                  return L"[不明]";
    }
}

inline const wchar_t* GetChatTypeLogFolderName(ChatType ct) {
    switch (ct) {
        case ChatType::General:   return L"一般チャット";
        case ChatType::WhisperRx: return L"ささやきチャット";
        case ChatType::Party:     return L"パーティチャット";
        case ChatType::Guild:     return L"ギルドチャット";
        case ChatType::All:       return L"全体チャット";
        case ChatType::ServerAll: return L"鯖全体";
        case ChatType::WhisperTx: return L"ささやきチャット";
        default:                  return L"その他";
    }
}

inline const wchar_t* GetChatTypeLogFilePrefix(ChatType ct) {
    switch (ct) {
        case ChatType::General:   return L"general";
        case ChatType::WhisperRx: return L"whisper_rx";
        case ChatType::Party:     return L"party";
        case ChatType::Guild:     return L"guild";
        case ChatType::All:       return L"all";
        case ChatType::ServerAll: return L"serverall";
        case ChatType::WhisperTx: return L"whisper_tx";
        default:                  return L"unknown";
    }
}


