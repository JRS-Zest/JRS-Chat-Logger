#include "flea_market_processor.h"
#include "item_dat_loader.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

static std::wstring ShiftJisToWide(const uint8_t* buf, int len) {
    if (len <= 0) return {};
    int wlen = MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(buf), len, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring ws(wlen, L'\0');
    MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(buf), len, ws.data(), wlen);
    return ws;
}

static std::string WideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), s.data(), len, nullptr, nullptr);
    return s;
}

static std::string EscapeJsonString(const std::wstring& ws) {
    std::string s = WideToUtf8(ws);
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
                std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned char>(c));
                out += hex;
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

static std::string EscapeCsv(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

static std::wstring NormalizeItemName(const std::wstring& name) {
    size_t start = 0;
    while (start < name.size() && (name[start] == L'\n' || name[start] == L'\r')) ++start;
    return name.substr(start);
}

static std::wstring TrimBrackets(std::wstring s) {
    if (s.size() >= 2 && s.front() == L'[' && s.back() == L']') return s.substr(1, s.size() - 2);
    return s;
}

static uint16_t ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

FleaMarketProcessor::FleaMarketProcessor(ItemDatLoader& itemDatLoader, int maxLinesPerFile)
    : itemDatLoader_(itemDatLoader)
    , maxLinesPerFile_(maxLinesPerFile)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    auto pos = exeDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) exeDir = exeDir.substr(0, pos);

    csvState_.dir = exeDir + L"\\ログ\\FleaMarket";
    jsonlState_.dir = exeDir + L"\\ログ\\露店価格";
    csvState_.prefix = L"flea_market_";
    csvState_.extension = L"csv";
    jsonlState_.prefix = L"flea_market_";
    jsonlState_.extension = L"jsonl";
    try { fs::create_directories(csvState_.dir); } catch (...) {}
    try { fs::create_directories(jsonlState_.dir); } catch (...) {}
}

void FleaMarketProcessor::ProcessPacket(const uint8_t* data, int len, bool /*isIncoming*/, int srcPort, int dstPort, uint32_t seq) {
    try {
        if (!data || len <= 0) return;

        // TCPセグメントは分割されて届くため、接続(ポートペア)ごとにバッファリングする
        uint32_t key = (static_cast<uint32_t>(srcPort & 0xFFFF) << 16) | static_cast<uint32_t>(dstPort & 0xFFFF);
        auto& st = streams_[key];

        // 再送セグメントを除外（同じseqの重複キャプチャ対策）
        if (!st.seqValid) {
            st.nextSeq = seq;
            st.seqValid = true;
        } else {
            int32_t diff = static_cast<int32_t>(seq - st.nextSeq);
            if (diff < 0) return;              // 再送
            if (diff > 0) {                    // 欠落/順序逆転 → 再同期
                st.buf.clear();
                st.nextSeq = seq;
            }
        }
        st.nextSeq = seq + static_cast<uint32_t>(len);

        auto& buf = st.buf;
        if (buf.size() + static_cast<size_t>(len) > 512 * 1024) buf.clear();
        buf.insert(buf.end(), data, data + len);

        int off = 0;
        while (off + 2 <= static_cast<int>(buf.size())) {
            uint16_t plen = ReadU16(buf.data() + off);
            if (plen < 0x16) {
                buf.clear();
                return;
            }
            if (off + plen > static_cast<int>(buf.size())) break; // パケット全体がまだ揃っていない
            ProcessSubPacket(buf.data() + off, plen);
            off += plen;
        }

        if (off > 0) {
            buf.erase(buf.begin(), buf.begin() + off);
        }
    } catch (...) {}
}

void FleaMarketProcessor::ProcessSubPacket(const uint8_t* data, int len) {
    if (data[2] != 0x28 || data[3] != 0x11) return;
    if (ReadU32(data + 0x0E) != 0x000012E4) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t tsBuf[64];
    swprintf(tsBuf, 64, L"%04d-%02d-%02dT%02d:%02d:%02d.%06d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds * 1000);
    std::wstring timestamp(tsBuf);

    int mainCount = ReadU16(data + 0x12);
    int pos = 0x16;

    for (int i = 0; i < mainCount; i++) {
        if (pos + 100 > len) return;
        Record rec;
        if (ParseRecord(data + pos, rec)) {
            SaveCsv(rec, st);
            SaveJsonl(rec, st, timestamp);
        }
        pos += 100;
    }

    while (pos + 10 <= len) {
        uint16_t subLen = ReadU16(data + pos);
        if (subLen < 10 || pos + subLen > len) break;
        uint32_t subcmd = ReadU32(data + pos + 2);
        uint16_t subCount = ReadU16(data + pos + 6);
        if (subcmd == 0x000012E4) {
            int rpos = pos + 10;
            for (int i = 0; i < subCount; i++) {
                if (rpos + 100 > len || rpos + 100 > pos + subLen) break;
                Record rec;
                if (ParseRecord(data + rpos, rec)) {
                    SaveCsv(rec, st);
                    SaveJsonl(rec, st, timestamp);
                }
                rpos += 100;
            }
        }
        pos += subLen;
    }
}

bool FleaMarketProcessor::ParseRecord(const uint8_t* rec, Record& out) const {
    out.serial = ReadU32(rec + 0x00);
    out.price = ReadU32(rec + 0x04);
    out.currency = ReadU16(rec + 0x0C);

    int nul = 18;
    for (int i = 0; i < 18; i++) {
        if (rec[0x10 + i] == 0) { nul = i; break; }
    }
    out.seller = ShiftJisToWide(rec + 0x10, nul);

    out.itemId = ReadU16(rec + 0x26);
    out.quantity = rec[0x28];

    out.options.clear();
    for (int pos = 0x2C; pos + 4 <= 100; pos += 4) {
        uint16_t id = ReadU16(rec + pos);
        uint16_t val = ReadU16(rec + pos + 2);
        if (id == 0xFFFF || id == 0x0000 || id == 0x0078 || id == 0x007A) break;
        out.options.push_back({id, val});
    }

    return out.serial != 0 && out.itemId != 0 && !out.seller.empty();
}

void FleaMarketProcessor::SaveCsv(const Record& rec, const SYSTEMTIME& st) {
    SetupCsvFile(st);
    if (csvState_.filePath.empty()) return;
    if (csvSeenSerials_.count(rec.serial)) return; // 同日に出力済みの出品はスキップ

    bool isNew = (csvState_.lineCount == 0);
    std::ofstream ofs(csvState_.filePath, std::ios::app | std::ios::binary);
    if (!ofs.is_open()) return;

    if (isNew) {
        ofs << "\xEF\xBB\xBF";
        ofs << "出品シリアル,売り手,アイテム名,オプション1,オプション2,オプション3,数量,価格,通貨\n";
    }

    std::wstring itemName = L"[" + std::to_wstring(rec.itemId) + L"]";
    const ItemInfo* info = itemDatLoader_.GetItemInfo(rec.itemId);
    if (info) itemName += NormalizeItemName(info->name);
    else itemName += L"ID:" + std::to_wstring(rec.itemId);

    auto formatOption = [&](const Record::Option& op) -> std::wstring {
        const OptionInfo* oi = itemDatLoader_.GetOptionInfo(op.id);
        std::wstring name = oi ? oi->name : (L"ID:" + std::to_wstring(op.id));
        name = TrimBrackets(name);

        // 職業スキル系(category=103): 下位バイトのみ表示（上位=系列は無視）
        if (oi && oi->category == 103) {
            int lv = op.value & 0xFF;
            return name + L" [" + std::to_wstring(lv) + L"]";
        }

        int lo = op.value & 0xFF;
        int hi = (op.value >> 8) & 0xFF;
        std::wstring val;
        if (hi != 0) {
            int mn = (std::min)(lo, hi);
            int mx = (std::max)(lo, hi);
            val = std::to_wstring(mn) + L"~" + std::to_wstring(mx);
        } else {
            val = std::to_wstring(lo);
        }
        return name + L" [" + val + L"]";
    };

    std::wstring opt[3];
    for (int i = 0; i < 3 && i < static_cast<int>(rec.options.size()); i++) {
        opt[i] = formatOption(rec.options[i]);
    }

    std::wstring currency = (rec.currency == 0) ? L"ゴールド"
                         : (rec.currency == 1) ? L"インゴット"
                         : L"?" + std::to_wstring(rec.currency);

    std::string line;
    line += std::to_string(rec.serial) + ",";
    line += EscapeCsv(WideToUtf8(rec.seller)) + ",";
    line += EscapeCsv(WideToUtf8(itemName)) + ",";
    line += EscapeCsv(WideToUtf8(opt[0])) + ",";
    line += EscapeCsv(WideToUtf8(opt[1])) + ",";
    line += EscapeCsv(WideToUtf8(opt[2])) + ",";
    line += std::to_string(rec.quantity) + ",";
    line += std::to_string(rec.price) + ",";
    line += EscapeCsv(WideToUtf8(currency)) + "\n";

    ofs << line;
    csvState_.lineCount++;
    csvSeenSerials_.insert(rec.serial);
}

void FleaMarketProcessor::SaveJsonl(const Record& rec, const SYSTEMTIME& st, const std::wstring& timestamp) {
    SetupFile(jsonlState_, st.wYear, st.wMonth, st.wDay);
    if (jsonlState_.filePath.empty()) return;

    std::ofstream ofs(jsonlState_.filePath, std::ios::app | std::ios::binary);
    if (!ofs.is_open()) return;

    std::wstring itemName;
    const ItemInfo* info = itemDatLoader_.GetItemInfo(rec.itemId);
    if (info) itemName = NormalizeItemName(info->name);
    else itemName = L"ID:" + std::to_wstring(rec.itemId);

    std::string optionsJson = "[";
    for (size_t i = 0; i < rec.options.size(); i++) {
        if (i > 0) optionsJson += ", ";
        std::wstring name = itemDatLoader_.GetOptionName(rec.options[i].id);
        if (name.empty()) name = L"ID:" + std::to_wstring(rec.options[i].id);
        name = TrimBrackets(name);
        optionsJson += EscapeJsonString(name);
    }
    optionsJson += "]";

    std::wstring currency = (rec.currency == 0) ? L"ゴールド"
                         : (rec.currency == 1) ? L"インゴット"
                         : L"?" + std::to_wstring(rec.currency);

    std::string json = "{\"timestamp\": " + EscapeJsonString(timestamp) +
                       ", \"item_name\": " + EscapeJsonString(itemName) +
                       ", \"item_count\": " + std::to_string(rec.quantity) +
                       ", \"options\": " + optionsJson +
                       ", \"price\": " + std::to_string(rec.price) +
                       ", \"currency\": " + EscapeJsonString(currency) + "}";

    ofs << json << "\n";
    jsonlState_.lineCount++;
}

void FleaMarketProcessor::SetupCsvFile(const SYSTEMTIME& st) {
    bool newDay = !csvState_.dateValid ||
                  csvState_.date[0] != st.wYear ||
                  csvState_.date[1] != st.wMonth ||
                  csvState_.date[2] != st.wDay;
    if (newDay) {
        csvState_.date[0] = st.wYear;
        csvState_.date[1] = st.wMonth;
        csvState_.date[2] = st.wDay;
        csvState_.dateValid = true;
        csvState_.fileIndex = 0;
        csvState_.filePath = GetFilePath(csvState_, st.wYear, st.wMonth, st.wDay, 0);
        csvState_.lineCount = CountLinesInFile(csvState_.filePath);

        // 当日の既存CSVから出品シリアルを読み込む（アプリ再起動時の二重追記防止）
        csvSeenSerials_.clear();
        int maxIdx = FindLatestFileIndex(csvState_, st.wYear, st.wMonth, st.wDay);
        for (int i = 0; i <= maxIdx; i++) {
            LoadCsvSerials(GetFilePath(csvState_, st.wYear, st.wMonth, st.wDay, i), csvSeenSerials_);
        }
    } else if (csvState_.filePath.empty()) {
        csvState_.filePath = GetFilePath(csvState_, st.wYear, st.wMonth, st.wDay, 0);
        csvState_.lineCount = CountLinesInFile(csvState_.filePath);
    }
}

void FleaMarketProcessor::LoadCsvSerials(const std::wstring& path, std::unordered_set<uint32_t>& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return;
    std::string line;
    bool first = true;
    while (std::getline(ifs, line)) {
        if (first) { first = false; continue; } // ヘッダ行
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto comma = line.find(',');
        if (comma == std::string::npos) continue;
        try {
            out.insert(static_cast<uint32_t>(std::stoul(line.substr(0, comma))));
        } catch (...) {}
    }
}

void FleaMarketProcessor::SetupFile(FileState& state, int year, int month, int day) {
    if (!state.dateValid || state.date[0] != year || state.date[1] != month || state.date[2] != day) {
        state.date[0] = year;
        state.date[1] = month;
        state.date[2] = day;
        state.dateValid = true;
        state.fileIndex = FindLatestFileIndex(state, year, month, day);
        std::wstring path = GetFilePath(state, year, month, day, state.fileIndex);
        state.lineCount = CountLinesInFile(path);
        if (state.lineCount >= maxLinesPerFile_) {
            state.fileIndex++;
            state.lineCount = 0;
        }
        state.filePath = GetFilePath(state, year, month, day, state.fileIndex);
    } else if (state.lineCount >= maxLinesPerFile_) {
        state.fileIndex++;
        state.lineCount = 0;
        state.filePath = GetFilePath(state, year, month, day, state.fileIndex);
    } else if (state.filePath.empty()) {
        state.filePath = GetFilePath(state, year, month, day, state.fileIndex);
    }
}

std::wstring FleaMarketProcessor::GetFilePath(const FileState& state, int year, int month, int day, int index) const {
    wchar_t buf[128];
    swprintf(buf, 128, L"%04d_%02d_%02d_%s%d.%s",
             year, month, day, state.prefix.c_str(), index, state.extension.c_str());
    return state.dir + L"\\" + buf;
}

int FleaMarketProcessor::FindLatestFileIndex(const FileState& state, int year, int month, int day) const {
    try {
        if (!fs::exists(state.dir)) return 0;
        wchar_t prefixBuf[64];
        swprintf(prefixBuf, 64, L"%04d_%02d_%02d_%s", year, month, day, state.prefix.c_str());
        std::wstring prefixStr(prefixBuf);
        std::wstring suffix = L"." + state.extension;

        int maxIdx = -1;
        for (auto& entry : fs::directory_iterator(state.dir)) {
            if (!entry.is_regular_file()) continue;
            auto name = entry.path().filename().wstring();
            if (name.size() <= prefixStr.size()) continue;
            if (name.substr(0, prefixStr.size()) != prefixStr) continue;
            auto part = name.substr(prefixStr.size());
            if (part.size() <= suffix.size()) continue;
            if (part.substr(part.size() - suffix.size()) != suffix) continue;
            part = part.substr(0, part.size() - suffix.size());
            try {
                int idx = std::stoi(part);
                if (idx > maxIdx) maxIdx = idx;
            } catch (...) {}
        }
        return maxIdx >= 0 ? maxIdx : 0;
    } catch (...) {
        return 0;
    }
}

int FleaMarketProcessor::CountLinesInFile(const std::wstring& path) const {
    try {
        if (!fs::exists(path)) return 0;
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) return 0;
        int c = 0;
        std::string line;
        while (std::getline(ifs, line)) c++;
        return c;
    } catch (...) {
        return 0;
    }
}
