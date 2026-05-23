#pragma once
#include "item_packet_processor.h"
#include <string>
#include <mutex>

// C# ItemLoggerService.cs の完全移植

class ItemLoggerService {
public:
    ItemLoggerService(int maxLinesPerFile = 1000);
    ~ItemLoggerService() = default;

    void LogItem(const ItemPickupInfo& info);

private:
    void SetupLogFile();
    std::wstring GetMonthFolderPath(const struct tm& date);
    std::wstring GetLogFileName(const struct tm& date, int index);
    int FindLatestFileIndex(const struct tm& date, const std::wstring& monthFolder);
    int CountLinesInFile(const std::wstring& filePath);
    static std::wstring EscapeCsv(const std::wstring& value);

    std::wstring baseLogDir_;
    std::wstring currentLogFile_;
    int currentDateDay_ = -1;
    int currentFileIndex_ = 0;
    int currentLineCount_ = 0;
    int maxLinesPerFile_;
    std::mutex lock_;
    bool disposed_ = false;
};
