#include "chat_logger_service.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// C# ChatLoggerService.cs の完全移植

namespace fs = std::filesystem;

ChatLoggerService::ChatLoggerService(int maxLinesPerFile)
    : maxLinesPerFile_(maxLinesPerFile)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    auto exeDir = fs::path(exePath).parent_path();
    baseLogDir_ = (exeDir / L"ログ").wstring();

    // 各チャット種別の状態を初期化
    for (int i = 0; i <= static_cast<int>(ChatType::WhisperTx); i++) {
        logStates_[i] = LogFileState{};
    }

    try { fs::create_directories(baseLogDir_); } catch (...) {}
}

static std::tm GetLocalTm(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm local{};
    localtime_s(&local, &t);
    return local;
}

static int DateToInt(const std::tm& tm) {
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

std::wstring ChatLoggerService::FormatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm local{};
    localtime_s(&local, &t);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()).count() % 1000;
    wchar_t buf[64];
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d.%03lld",
               local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
               local.tm_hour, local.tm_min, local.tm_sec, ms);
    return buf;
}

std::wstring ChatLoggerService::EscapeCsvField(const std::wstring& field) {
    if (field.empty()) return L"";
    bool needQuote = false;
    for (auto c : field) {
        if (c == L'"' || c == L',' || c == L'\n' || c == L'\r') {
            needQuote = true;
            break;
        }
    }
    if (!needQuote) return field;

    std::wstring result = L"\"";
    for (auto c : field) {
        if (c == L'"') result += L"\"\"";
        else result += c;
    }
    result += L"\"";
    return result;
}

void ChatLoggerService::Log(const ChatMessage& msg) {
    if (disposed_) return;
    std::lock_guard<std::mutex> lk(lock_);
    try {
        auto& state = logStates_[static_cast<int>(msg.chatType)];
        auto local = GetLocalTm(msg.timestamp);
        SetupLogFile(msg.chatType, state, local);
        if (state.currentFilePath.empty()) return;

        // CSV行: Timestamp,ChatType,SenderName,Message
        auto ts = FormatTimestamp(msg.timestamp);
        auto chatTypeName = GetChatTypeLogFilePrefix(msg.chatType);
        auto line = ts + L"," + chatTypeName + L"," +
                    EscapeCsvField(msg.senderName) + L"," +
                    EscapeCsvField(msg.message) + L"\n";

        // UTF-8で書き込み
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::string utf8(utf8Len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, utf8.data(), utf8Len, nullptr, nullptr);

            std::ofstream f(state.currentFilePath, std::ios::app | std::ios::binary);
            if (f) {
                f.write(utf8.data(), utf8.size());
                state.currentLineCount++;
            }
        }
    } catch (...) {}
}

void ChatLoggerService::SetupLogFile(ChatType chatType, LogFileState& state, const std::tm& now) {
    int currentDateInt = DateToInt(now);

    // 月別フォルダ: yyyy年MM月分
    wchar_t monthBuf[32];
    swprintf_s(monthBuf, L"%04d年%02d月分", now.tm_year + 1900, now.tm_mon + 1);
    auto logDir = fs::path(baseLogDir_) / GetChatTypeLogFolderName(chatType) / monthBuf;
    try { fs::create_directories(logDir); } catch (...) { return; }

    // 日付が変わった場合
    if (state.currentDateDay != currentDateInt) {
        state.currentDateDay = currentDateInt;
        state.currentFileIndex = FindLatestFileIndex(logDir.wstring(), now, chatType);

        auto logFilePath = (logDir / GetLogFileName(now, chatType, state.currentFileIndex)).wstring();
        state.currentLineCount = CountLinesInFile(logFilePath);

        if (state.currentLineCount >= maxLinesPerFile_) {
            state.currentFileIndex++;
            state.currentLineCount = 0;
        }
        state.currentFilePath = (logDir / GetLogFileName(now, chatType, state.currentFileIndex)).wstring();
    }
    // 最大行数到達
    else if (state.currentLineCount >= maxLinesPerFile_) {
        state.currentFileIndex++;
        state.currentLineCount = 0;
        state.currentFilePath = (logDir / GetLogFileName(now, chatType, state.currentFileIndex)).wstring();
    }
    // ファイルパス未設定（初回）
    else if (state.currentFilePath.empty()) {
        state.currentFilePath = (logDir / GetLogFileName(now, chatType, state.currentFileIndex)).wstring();
    }
}

std::wstring ChatLoggerService::GetLogFileName(const std::tm& date, ChatType chatType, int index) {
    wchar_t buf[128];
    auto prefix = GetChatTypeLogFilePrefix(chatType);
    swprintf_s(buf, L"%04d_%02d_%02d_%s_%d.csv",
               date.tm_year + 1900, date.tm_mon + 1, date.tm_mday,
               prefix, index);
    return buf;
}

int ChatLoggerService::FindLatestFileIndex(const std::wstring& logDir, const std::tm& date, ChatType chatType) {
    wchar_t prefixBuf[64];
    auto pfx = GetChatTypeLogFilePrefix(chatType);
    swprintf_s(prefixBuf, L"%04d_%02d_%02d_%s_",
               date.tm_year + 1900, date.tm_mon + 1, date.tm_mday, pfx);
    std::wstring prefix = prefixBuf;
    int maxIndex = -1;

    try {
        if (fs::exists(logDir)) {
            for (auto& entry : fs::directory_iterator(logDir)) {
                if (!entry.is_regular_file()) continue;
                auto fname = entry.path().stem().wstring();
                if (fname.find(prefix) == 0) {
                    auto indexPart = fname.substr(prefix.length());
                    try {
                        int idx = std::stoi(indexPart);
                        if (idx > maxIndex) maxIndex = idx;
                    } catch (...) {}
                }
            }
        }
    } catch (...) {}

    return maxIndex >= 0 ? maxIndex : 0;
}

int ChatLoggerService::CountLinesInFile(const std::wstring& filePath) {
    try {
        if (!fs::exists(filePath)) return 0;
        std::ifstream f(filePath);
        if (!f) return 0;
        int count = 0;
        std::string line;
        while (std::getline(f, line)) count++;
        return count;
    } catch (...) {}
    return 0;
}
