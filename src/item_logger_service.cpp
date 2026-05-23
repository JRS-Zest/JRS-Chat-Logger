#include "item_logger_service.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// C# ItemLoggerService.cs の完全移植

namespace fs = std::filesystem;

ItemLoggerService::ItemLoggerService(int maxLinesPerFile)
    : maxLinesPerFile_(maxLinesPerFile)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    auto exeDir = fs::path(exePath).parent_path();
    baseLogDir_ = (exeDir / L"ログ" / L"拾得アイテム").wstring();
    try { fs::create_directories(baseLogDir_); } catch (...) {}
}

static std::tm GetLocalTmNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &t);
    return local;
}

static int TmToDateInt(const std::tm& tm) {
    return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

std::wstring ItemLoggerService::GetMonthFolderPath(const std::tm& date) {
    wchar_t buf[32];
    swprintf_s(buf, L"%04d年%02d月分", date.tm_year + 1900, date.tm_mon + 1);
    return (fs::path(baseLogDir_) / buf).wstring();
}

std::wstring ItemLoggerService::GetLogFileName(const std::tm& date, int index) {
    wchar_t buf[64];
    swprintf_s(buf, L"%04d_%02d_%02d_item_%d.csv",
               date.tm_year + 1900, date.tm_mon + 1, date.tm_mday, index);
    return buf;
}

int ItemLoggerService::FindLatestFileIndex(const std::tm& date, const std::wstring& monthFolder) {
    wchar_t prefixBuf[64];
    swprintf_s(prefixBuf, L"%04d_%02d_%02d_item_",
               date.tm_year + 1900, date.tm_mon + 1, date.tm_mday);
    std::wstring prefix = prefixBuf;
    int maxIndex = -1;

    try {
        if (fs::exists(monthFolder)) {
            for (auto& entry : fs::directory_iterator(monthFolder)) {
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

int ItemLoggerService::CountLinesInFile(const std::wstring& filePath) {
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

std::wstring ItemLoggerService::EscapeCsv(const std::wstring& value) {
    bool needQuote = false;
    for (auto c : value) {
        if (c == L',' || c == L'"' || c == L'\n') {
            needQuote = true;
            break;
        }
    }
    if (!needQuote) return value;
    std::wstring result = L"\"";
    for (auto c : value) {
        if (c == L'"') result += L"\"\"";
        else result += c;
    }
    result += L"\"";
    return result;
}

void ItemLoggerService::SetupLogFile() {
    auto now = GetLocalTmNow();
    int currentDateInt = TmToDateInt(now);
    auto monthFolder = GetMonthFolderPath(now);

    try { fs::create_directories(monthFolder); } catch (...) {}

    // 日付が変わった場合
    if (currentDateDay_ != currentDateInt) {
        currentDateDay_ = currentDateInt;
        currentFileIndex_ = FindLatestFileIndex(now, monthFolder);

        auto logFilePath = (fs::path(monthFolder) / GetLogFileName(now, currentFileIndex_)).wstring();
        currentLineCount_ = CountLinesInFile(logFilePath);

        if (currentLineCount_ >= maxLinesPerFile_) {
            currentFileIndex_++;
            currentLineCount_ = 0;
        }
        currentLogFile_ = (fs::path(monthFolder) / GetLogFileName(now, currentFileIndex_)).wstring();
    }
    else if (currentLineCount_ >= maxLinesPerFile_) {
        currentFileIndex_++;
        currentLineCount_ = 0;
        auto monthNow = GetMonthFolderPath(now);
        currentLogFile_ = (fs::path(monthNow) / GetLogFileName(now, currentFileIndex_)).wstring();
    }
    else if (currentLogFile_.empty()) {
        currentLogFile_ = (fs::path(monthFolder) / GetLogFileName(now, currentFileIndex_)).wstring();
    }
}

void ItemLoggerService::LogItem(const ItemPickupInfo& info) {
    std::lock_guard<std::mutex> lk(lock_);
    SetupLogFile();
    if (currentLogFile_.empty()) return;

    try {
        // タイムスタンプ
        auto t = std::chrono::system_clock::to_time_t(info.timestamp);
        std::tm local{};
        localtime_s(&local, &t);
        wchar_t tsBuf[32];
        swprintf_s(tsBuf, L"%04d-%02d-%02d %02d:%02d:%02d",
                   local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                   local.tm_hour, local.tm_min, local.tm_sec);

        // オプション結合
        std::wstring optStr;
        for (size_t i = 0; i < info.options.size(); i++) {
            if (i > 0) optStr += L", ";
            optStr += info.options[i];
        }

        std::wstring line = std::wstring(tsBuf) + L"," +
                            EscapeCsv(info.itemName) + L"," +
                            EscapeCsv(optStr) + L"," +
                            (info.isNotificationTarget ? L"True" : L"False") + L"\n";

        // CSVヘッダー追加（新規ファイルの場合）
        bool needHeader = !fs::exists(currentLogFile_) || currentLineCount_ == 0;

        // UTF-8で書き込み
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::string utf8(utf8Len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, utf8.data(), utf8Len, nullptr, nullptr);

            std::ofstream f(currentLogFile_, std::ios::app | std::ios::binary);
            if (f) {
                if (needHeader) {
                    std::string header = "Timestamp,ItemName,Options,IsNotification\n";
                    f.write(header.data(), header.size());
                    currentLineCount_ = 1;
                }
                f.write(utf8.data(), utf8.size());
                currentLineCount_++;
            }
        }
    } catch (...) {}
}
