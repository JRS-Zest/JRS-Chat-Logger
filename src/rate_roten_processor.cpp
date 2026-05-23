#include "rate_roten_processor.h"
#include "item_dat_loader.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

// Shift-JIS → wstring
static std::wstring ShiftJisToWide(const uint8_t* buf, int len) {
    if (len <= 0) return {};
    int wlen = MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(buf), len, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring ws(wlen, L'\0');
    MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(buf), len, ws.data(), wlen);
    return ws;
}

// JSON文字列エスケープ
static std::string EscapeJsonString(const std::wstring& ws) {
    // wstring → UTF-8
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(utf8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), utf8len, nullptr, nullptr);

    std::string out;
    out.reserve(s.size() + 8);
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char hex[8];
                std::snprintf(hex, sizeof(hex), "\\u%04x", (unsigned char)c);
                out += hex;
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

// item.dat不整合対策: 先頭に混入した改行コードのみ取り除く
static std::wstring NormalizeItemName(const std::wstring& name) {
    size_t start = 0;
    while (start < name.size() && (name[start] == L'\n' || name[start] == L'\r')) {
        ++start;
    }
    return name.substr(start);
}

RateRotenProcessor::RateRotenProcessor(ItemDatLoader& itemDatLoader, int maxLinesPerFile)
    : itemDatLoader_(itemDatLoader)
    , maxLinesPerFile_(maxLinesPerFile)
{
    // ログディレクトリ作成
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    auto pos = exeDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) exeDir = exeDir.substr(0, pos);

    logDir_ = exeDir + L"\\ログ\\露店価格";
    try { fs::create_directories(logDir_); } catch (...) {}
}

void RateRotenProcessor::ResetState() {
    // 将来必要なら状態をリセット
}

void RateRotenProcessor::ProcessPacket(const uint8_t* data, int len, bool /*isIncoming*/, int /*srcPort*/, int /*dstPort*/) {
    try {
        if (!data || len < 100) return;
        if (len < 4) return;
        if (data[2] != COMMAND_ID_0 || data[3] != COMMAND_ID_1) return;

        int subcmdPos = FindSequence(data, len, SUB_COMMAND_BYTES, 6, 0);
        if (subcmdPos == -1) return;

        std::wstring charName;
        std::vector<RateRotenItem> items;
        if (!ParseFromSubcmd(data, len, subcmdPos, charName, items)) return;

        // デバッグHEXダンプ: アイテム名が "serial+" で始まっている場合
        try {
            for (auto& it : items) {
                if (it.itemName.size() >= 7 && it.itemName.substr(0, 7) == L"serial+") {
                    // DumpDebugHex(data, len, srcPort, dstPort, charName);
                    break;
                }
            }
        } catch (...) {}

        // タイムスタンプ取得
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t tsBuf[64];
        std::swprintf(tsBuf, 64, L"%04d-%02d-%02dT%02d:%02d:%02d.%06d",
                      st.wYear, st.wMonth, st.wDay,
                      st.wHour, st.wMinute, st.wSecond,
                      st.wMilliseconds * 1000);
        std::wstring timestamp(tsBuf);

        // JSONL ログ保存
        for (auto& it : items) {
            SaveItemAsJson(it, timestamp);
        }

        // UIコールバック
        if (callback_) {
            RateRotenResult result;
            result.charName = charName;
            result.items = items;
            callback_(result);
        }
    } catch (...) {
        // 解析失敗しても落とさない
    }
}

bool RateRotenProcessor::ParseFromSubcmd(const uint8_t* data, int len, int subcmdPos,
                                          std::wstring& outCharName, std::vector<RateRotenItem>& outItems) {
    try {
        int charNameStart = subcmdPos + CHAR_NAME_OFFSET_FROM_SUBCMD;
        if (charNameStart + CHAR_NAME_FIELD_SIZE > len) return false;

        // NULL終端を探す
        int nul = -1;
        for (int i = 0; i < CHAR_NAME_FIELD_SIZE; i++) {
            if (data[charNameStart + i] == 0) { nul = i; break; }
        }
        if (nul <= 0) return false;

        outCharName = ShiftJisToWide(data + charNameStart, nul);
        if (outCharName.empty() || (int)outCharName.size() > 20) return false;

        int slotStartPos = charNameStart + CHAR_NAME_FIELD_SIZE;

        while (slotStartPos + SLOT_SIZE <= len) {
            const uint8_t* slotData = data + slotStartPos;

            uint8_t slotNumber = slotData[36];
            if (slotNumber > 8) break;

            uint16_t itemSerial = slotData[4] | (slotData[5] << 8);
            if (itemSerial == 0 || itemSerial == 0xCCCC) break;

            std::wstring itemName = NormalizeItemName(itemDatLoader_.GetItemName(itemSerial));
            if (itemName.empty()) {
                // 名称が空の場合のみ除外
                slotStartPos += SLOT_SIZE;
                continue;
            }
            uint8_t itemCount = slotData[6];

            auto options = ParseOptions(slotData + 10, 12);

            uint32_t price = slotData[32] | (slotData[33] << 8) | (slotData[34] << 16) | (slotData[35] << 24);
            uint8_t priceTypeVal = slotData[38];
            std::wstring priceType = (priceTypeVal == 0) ? L"ゴールド" : L"インゴット";

            RateRotenItem item;
            item.slot = slotNumber;
            item.itemName = itemName;
            item.count = itemCount;
            item.options = options;
            item.price = price;
            item.currency = priceType;
            outItems.push_back(std::move(item));

            slotStartPos += SLOT_SIZE;
        }

        return !outItems.empty();
    } catch (...) {
        return false;
    }
}

std::vector<std::wstring> RateRotenProcessor::ParseOptions(const uint8_t* optionsBytes, int optLen) {
    std::vector<std::wstring> list;
    int i = 0;
    while (i + 2 <= optLen) {
        uint16_t id = optionsBytes[i] | (optionsBytes[i + 1] << 8);
        if (id == 0xFFFF) break;
        std::wstring name = itemDatLoader_.GetOptionName(id);
        if (!name.empty()) list.push_back(name);
        i += 4; // id + value
    }
    return list;
}

int RateRotenProcessor::FindSequence(const uint8_t* data, int dataLen, const uint8_t* seq, int seqLen, int start) {
    if (start < 0 || seqLen <= 0) return -1;
    for (int i = start; i <= dataLen - seqLen; i++) {
        bool ok = true;
        for (int j = 0; j < seqLen; j++) {
            if (data[i + j] != seq[j]) { ok = false; break; }
        }
        if (ok) return i;
    }
    return -1;
}

void RateRotenProcessor::SaveItemAsJson(const RateRotenItem& item, const std::wstring& timestamp) {
    try {
        std::wstring normalizedName = NormalizeItemName(item.itemName);
        if (normalizedName.empty()) return;

        SYSTEMTIME st;
        GetLocalTime(&st);
        SetupJsonLogFile(st.wYear, st.wMonth, st.wDay);
        if (currentLogFilePath_.empty()) return;

        std::string tsJson = EscapeJsonString(timestamp);
        std::string nameJson = EscapeJsonString(normalizedName);
        std::string currencyJson = EscapeJsonString(item.currency);

        // options配列
        std::string optionsJson;
        if (item.options.empty()) {
            optionsJson = "[]";
        } else {
            optionsJson = "[";
            for (size_t i = 0; i < item.options.size(); i++) {
                if (i > 0) optionsJson += ", ";
                // [] を除去
                std::wstring opt = item.options[i];
                if (opt.size() >= 2 && opt.front() == L'[' && opt.back() == L']') {
                    opt = opt.substr(1, opt.size() - 2);
                }
                optionsJson += EscapeJsonString(opt);
            }
            optionsJson += "]";
        }

        std::string jsonLine = "{\"timestamp\": " + tsJson +
                               ", \"item_name\": " + nameJson +
                               ", \"item_count\": " + std::to_string(item.count) +
                               ", \"options\": " + optionsJson +
                               ", \"price\": " + std::to_string(item.price) +
                               ", \"currency\": " + currencyJson + "}";

        // UTF-8 BOM なしで追記
        std::ofstream ofs(currentLogFilePath_, std::ios::app | std::ios::binary);
        if (ofs.is_open()) {
            ofs << jsonLine << "\n";
            currentLineCount_++;
        }
    } catch (...) {
        // ファイル書き込みエラーは無視
    }
}

void RateRotenProcessor::SetupJsonLogFile(int year, int month, int day) {
    if (!dateValid_ || currentDate_[0] != year || currentDate_[1] != month || currentDate_[2] != day) {
        currentDate_[0] = year;
        currentDate_[1] = month;
        currentDate_[2] = day;
        dateValid_ = true;
        currentFileIndex_ = FindLatestFileIndex(year, month, day);
        auto path = GetLogFilePath(year, month, day, currentFileIndex_);
        currentLineCount_ = CountLinesInFile(path);
        if (currentLineCount_ >= maxLinesPerFile_) {
            currentFileIndex_++;
            currentLineCount_ = 0;
        }
        currentLogFilePath_ = GetLogFilePath(year, month, day, currentFileIndex_);
    } else if (currentLineCount_ >= maxLinesPerFile_) {
        currentFileIndex_++;
        currentLineCount_ = 0;
        currentLogFilePath_ = GetLogFilePath(year, month, day, currentFileIndex_);
    } else if (currentLogFilePath_.empty()) {
        currentLogFilePath_ = GetLogFilePath(year, month, day, currentFileIndex_);
    }
}

std::wstring RateRotenProcessor::GetLogFilePath(int year, int month, int day, int index) {
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%04d_%02d_%02d_rate_roten_%d.jsonl", year, month, day, index);
    return logDir_ + L"\\" + buf;
}

int RateRotenProcessor::FindLatestFileIndex(int year, int month, int day) {
    try {
        if (!fs::exists(logDir_)) return 0;
        wchar_t prefix[32];
        std::swprintf(prefix, 32, L"%04d_%02d_%02d_rate_roten_", year, month, day);
        std::wstring prefixStr(prefix);

        int maxIdx = -1;
        for (auto& entry : fs::directory_iterator(logDir_)) {
            if (!entry.is_regular_file()) continue;
            auto name = entry.path().filename().wstring();
            if (name.size() > prefixStr.size() && name.substr(0, prefixStr.size()) == prefixStr) {
                auto part = name.substr(prefixStr.size());
                if (part.size() > 6 && part.substr(part.size() - 6) == L".jsonl") {
                    part = part.substr(0, part.size() - 6);
                    try {
                        int idx = std::stoi(part);
                        if (idx > maxIdx) maxIdx = idx;
                    } catch (...) {}
                }
            }
        }
        return maxIdx >= 0 ? maxIdx : 0;
    } catch (...) {
        return 0;
    }
}

int RateRotenProcessor::CountLinesInFile(const std::wstring& path) {
    try {
        if (!fs::exists(path)) return 0;
        std::ifstream ifs(path);
        if (!ifs.is_open()) return 0;
        int c = 0;
        std::string line;
        while (std::getline(ifs, line)) c++;
        return c;
    } catch (...) {
        return 0;
    }
}

void RateRotenProcessor::DumpDebugHex(const uint8_t* data, int len, int srcPort, int dstPort, const std::wstring& charName) {
    try {
        if (debugDir_.empty()) return;
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t fileName[128];
        std::swprintf(fileName, 128, L"%04d%02d%02d_%02d-%02d-%02d-%03d_%d_%d.hex",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                      st.wMilliseconds, srcPort, dstPort);
        std::wstring path = debugDir_ + L"\\" + fileName;

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) return;

        // ヘッダ行
        std::string charNarrow;
        {
            int n = WideCharToMultiByte(CP_UTF8, 0, charName.c_str(), (int)charName.size(), nullptr, 0, nullptr, nullptr);
            charNarrow.resize(n);
            WideCharToMultiByte(CP_UTF8, 0, charName.c_str(), (int)charName.size(), charNarrow.data(), n, nullptr, nullptr);
        }
        ofs << "# char:" << charNarrow << " srcPort:" << srcPort << " dstPort:" << dstPort << "\n";

        // HEXダンプ
        for (int i = 0; i < len; i++) {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%02X", data[i]);
            if (i > 0) ofs << ' ';
            ofs << hex;
        }
        ofs << "\n";
    } catch (...) {}
}
