#pragma once
#include "chat_packet_processor.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

// C# ChatLoggerService.cs の完全移植
// チャット種別ごとにCSVファイルに保存
// ChatMessage は chat_packet_processor.h で定義済み

class ChatLoggerService {
public:
    ChatLoggerService(int maxLinesPerFile = 1000);
    ~ChatLoggerService() = default;

    void Log(const ChatMessage& msg);

private:
    struct LogFileState {
        std::wstring currentFilePath;
        int currentDateDay = -1;  // YYYYMMDD形式の日
        int currentFileIndex = 0;
        int currentLineCount = 0;
    };

    void SetupLogFile(ChatType chatType, LogFileState& state, const std::tm& now);
    static std::wstring GetLogFileName(const std::tm& date, ChatType chatType, int index);
    static int FindLatestFileIndex(const std::wstring& logDir, const std::tm& date, ChatType chatType);
    static int CountLinesInFile(const std::wstring& filePath);
    static std::wstring EscapeCsvField(const std::wstring& field);
    static std::wstring FormatTimestamp(const std::chrono::system_clock::time_point& tp);

    std::wstring baseLogDir_;
    int maxLinesPerFile_;
    std::unordered_map<int, LogFileState> logStates_;
    std::mutex lock_;
    bool disposed_ = false;
};
