// guild_member_processor.cpp — ギルド員一覧パケット (0x1128 / 0x1203) 解析
#include "guild_member_processor.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace fs = std::filesystem;

GuildMemberProcessor::GuildMemberProcessor() {}

// ─────────── Shift_JIS → wstring ───────────
std::wstring GuildMemberProcessor::SjisToWide(const uint8_t* data, int maxLen) {
    // null 終端までの実長を算出
    int slen = 0;
    while (slen < maxLen && data[slen] != 0) slen++;
    if (slen == 0) return {};

    int wlen = MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(data), slen, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring ws(wlen, L'\0');
    MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(data), slen, ws.data(), wlen);
    return ws;
}

// ─────────── 職業コード → 名前 ───────────
std::wstring GuildMemberProcessor::JobToName(uint8_t job) {
    switch (job) {
    case 0x00: return L"剣士";
    case 0x01: return L"戦士";
    case 0x02: return L"ウィザード";
    case 0x03: return L"ウルフマン";
    case 0x04: return L"ビショップ";
    case 0x05: return L"追放天使";
    case 0x06: return L"シーフ";
    case 0x07: return L"武道家";
    case 0x08: return L"ランサー";
    case 0x09: return L"アーチャー";
    case 0x0A: return L"ビーストテイマー";
    case 0x0B: return L"サマナー";
    case 0x0C: return L"プリンセス";
    case 0x0D: return L"リトルウィッチ";
    case 0x0E: return L"ネクロマンサー";
    case 0x0F: return L"悪魔";
    case 0x10: return L"霊術師";
    case 0x11: return L"闘士";
    case 0x12: return L"光奏師";
    default: {
        wchar_t buf[16];
        swprintf_s(buf, L"%d", job);
        return buf;
    }
    }
}

// ─────────── 役職コード → 名前 ───────────
std::wstring GuildMemberProcessor::RankToName(uint8_t rank) {
    switch (rank) {
    case 0x60: return L"マスター";
    case 0x50: return L"元老";
    case 0x40: return L"副マス";
    case 0x30: return L"委員";
    case 0x20: return L"一般";
    case 0x10: return L"新入";
    default: {
        wchar_t buf[16];
        swprintf_s(buf, L"%d", rank);
        return buf;
    }
    }
}

// ─────────── パケット受信エントリ（再結合バッファ付き） ───────────
void GuildMemberProcessor::ProcessPacket(const uint8_t* payload, int len,
                                         bool isIncoming, int /*srcPort*/, int /*dstPort*/) {
    if (!isIncoming) return;
    if (len <= 0) return;

    // 受信データをバッファに蓄積して処理
    int offset = 0;
    while (offset < len) {
        // ── ヘッダー待ち: 先頭2バイト(TotalLength)を取得 ──
        if (waitingHeader_) {
            // バッファにまだヘッダーが揃っていない
            int need = 2 - (int)reassemblyBuf_.size();
            int avail = len - offset;
            int take = (std::min)(need, avail);
            reassemblyBuf_.insert(reassemblyBuf_.end(), payload + offset, payload + offset + take);
            offset += take;

            if ((int)reassemblyBuf_.size() < 2) return; // まだヘッダー不足

            // TotalLength 取得
            expectedLen_ = *reinterpret_cast<const uint16_t*>(reassemblyBuf_.data());

            // 異常値ガード (0 または極端に大きい値)
            if (expectedLen_ < 4 || expectedLen_ > 0xFFFF) {
                reassemblyBuf_.clear();
                expectedLen_ = 0;
                waitingHeader_ = true;
                return; // 破棄
            }
            waitingHeader_ = false;
        }

        // ── ボディ蓄積: TotalLength 分が揃うまでバッファリング ──
        int remaining = (int)expectedLen_ - (int)reassemblyBuf_.size();
        int avail = len - offset;
        int take = (std::min)(remaining, avail);
        reassemblyBuf_.insert(reassemblyBuf_.end(), payload + offset, payload + offset + take);
        offset += take;

        if ((int)reassemblyBuf_.size() < (int)expectedLen_) return; // まだ不足

        // ── 完成: TotalLength 分のデータが揃った ──
        ProcessReassembledPacket(reassemblyBuf_.data(), (int)reassemblyBuf_.size());

        // バッファリセット (次のパケットへ)
        reassemblyBuf_.clear();
        expectedLen_ = 0;
        waitingHeader_ = true;
        // offset 以降の残余データはループで次のパケットとして処理
    }
}

// ─────────── 再結合済みパケットの解析 ───────────
void GuildMemberProcessor::ProcessReassembledPacket(const uint8_t* data, int len) {
    // 構造: [2B TotalLength][2B TotalID][4B Pad][4B InnerCount][Inner...]
    // ヘッダ = 12バイト
    if (len < 12) return;

    uint16_t totalID = *reinterpret_cast<const uint16_t*>(data + 2);
    if (totalID != 0x1128) return; // 0x1128 コンテナ以外は無視

    // 古い pending があれば先にフラッシュ (5秒超過)
    if (!pendingMembers_.empty()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - pendingTimestamp_).count();
        if (elapsed > 5) {
            TryFireCallback(); // ギルド名なしでも発火
        }
    }

    // 内部データ: ヘッダ(12B) の後から
    const uint8_t* inner = data + 12;
    int innerLen = len - 12;

    AnalyzeOuterPacket(inner, innerLen);
}

// ─────────── アウター内部走査 ───────────
void GuildMemberProcessor::AnalyzeOuterPacket(const uint8_t* data, int len) {
    // Inner パケットを [2B InnerLen][2B InnerID][...] でループ走査

    int pos = 0;
    while (pos + 4 <= len) {
        uint16_t innerLen = *reinterpret_cast<const uint16_t*>(data + pos);
        uint16_t innerID  = *reinterpret_cast<const uint16_t*>(data + pos + 2);

        if (innerLen < 4) break;                        // 異常
        if (pos + innerLen > len) break;                // 境界超過

        const uint8_t* body = data + pos + 4;           // InnerID の後ろ
        int bodyLen = innerLen - 4;                      // ヘッダ(4B)除く

        // ① 判定トリガー (InnerID == 0x1203) — 新セッション開始
        if (innerID == 0x1203) {
            pendingMembers_.clear();
            pendingGuildName_.clear();
        }

        // ② ギルド員一覧 (InnerID == 0x1202)
        //    初回ヘッダ: [2B Reserved][1B BlockCount][1B BlockFlag=0x00] = 4バイト
        //    継続ヘッダ: [2B Reserved][1B BlockCount][1B BlockFlag=0x80][4B Unknown] = 8バイト
        //    各ブロック最大50名。50未満なら最終ブロック。
        if (innerID == 0x1202 && bodyLen >= 4) {
            int bpos = 0;
            // 初回ヘッダ (4バイト)
            uint8_t blockCount = body[bpos + 2];
            bpos += 4;

            while (true) {
                for (int bi = 0; bi < blockCount; bi++) {
                    if (bpos + 25 > bodyLen) goto done_1202;

                    GuildMemberInfo m;
                    m.name = SjisToWide(body + bpos, 17);
                    m.jobCode = body[bpos + 17];
                    m.rankCode = body[bpos + 18];
                    uint16_t lvRaw = *reinterpret_cast<const uint16_t*>(body + bpos + 19);
                    m.level = static_cast<int>(lvRaw) - 0xC000;

                    m.jobName = JobToName(m.jobCode);
                    m.rankName = RankToName(m.rankCode);

                    pendingMembers_.push_back(std::move(m));
                    bpos += 25;
                }

                // 終了判定: BlockCount < 50 なら最終ブロック
                if (blockCount < 50) break;
                // 継続ヘッダ (8バイト)
                if (bpos + 8 > bodyLen) break;
                blockCount = body[bpos + 2];
                bpos += 8;
            }
            done_1202:
            if (!pendingMembers_.empty()) {
                pendingTimestamp_ = std::chrono::system_clock::now();
            }
        }

        // ③ ギルド名 (InnerID == 0x1246)
        //    body内を逆走査して最後の FFFFFFFF の直後にある文字列を抽出
        if (innerID == 0x1246 && bodyLen >= 8) {
            for (int si = bodyLen - 5; si >= 0; si--) {
                if (body[si] == 0xFF && body[si + 1] == 0xFF &&
                    body[si + 2] == 0xFF && body[si + 3] == 0xFF) {
                    int nameStart = si + 4;
                    if (nameStart < bodyLen) {
                        uint8_t next = body[nameStart];
                        // FFFFFFFF 直後が 0xFF や 0x00 なら偽マーカー → スキップ
                        if (next != 0xFF && next != 0x00) {
                            int nameMaxLen = bodyLen - nameStart;
                            pendingGuildName_ = SjisToWide(body + nameStart, nameMaxLen);
                            break;
                        }
                    }
                }
            }
        }

        pos += innerLen;
    }

    // メンバーとギルド名が揃ったらコールバック発火
    TryFireCallback();
}

// ─────────── コールバック発火判定 ───────────
void GuildMemberProcessor::TryFireCallback() {
    if (pendingMembers_.empty()) return;

    // ギルド名がまだ空で、5秒以内なら次のパケットを待つ
    if (pendingGuildName_.empty()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - pendingTimestamp_).count();
        if (elapsed <= 5) return; // まだ待つ
    }

    GuildMemberResult result;
    result.timestamp = pendingTimestamp_;
    result.guildName = pendingGuildName_;
    result.members = std::move(pendingMembers_);

    pendingMembers_.clear();
    pendingGuildName_.clear();

    if (callback_) {
        callback_(result);
    }
}

// ─────────── フォルダ内の既存ファイルから最終保存時刻を復元 ───────────
void GuildMemberProcessor::LoadLastSaveTimesFromFolder(const std::wstring& folderPath) {
    if (!fs::exists(folderPath)) return;

    for (auto& entry : fs::directory_iterator(folderPath)) {
        if (!entry.is_regular_file()) continue;
        auto fname = entry.path().filename().wstring();
        // ファイル形式: YYYYMMDD_HHMMSS_ギルド名.csv
        // 最低 "YYYYMMDD_HHMMSS_X.csv" = 20文字
        if (fname.size() < 20) continue;
        if (fname.substr(fname.size() - 4) != L".csv") continue;

        // タイムスタンプ部分: YYYYMMDD_HHMMSS (15文字)
        auto tsStr = fname.substr(0, 15);
        // ギルド名: 16文字目から .csv の前まで
        auto gname = fname.substr(16, fname.size() - 16 - 4);
        if (gname.empty()) continue;

        // タイムスタンプのパース
        std::tm tm{};
        if (tsStr.size() == 15 && tsStr[8] == L'_') {
            tm.tm_year = std::stoi(tsStr.substr(0, 4)) - 1900;
            tm.tm_mon  = std::stoi(tsStr.substr(4, 2)) - 1;
            tm.tm_mday = std::stoi(tsStr.substr(6, 2));
            tm.tm_hour = std::stoi(tsStr.substr(9, 2));
            tm.tm_min  = std::stoi(tsStr.substr(11, 2));
            tm.tm_sec  = std::stoi(tsStr.substr(13, 2));
            tm.tm_isdst = -1;
            time_t t = std::mktime(&tm);
            if (t != -1) {
                auto tp = std::chrono::system_clock::from_time_t(t);
                // 既存の方が新しければ更新
                auto it = lastSaveMap_.find(gname);
                if (it == lastSaveMap_.end() || tp > it->second) {
                    lastSaveMap_[gname] = tp;
                }
            }
        }
    }
}

// ─────────── CSV保存 (間隔制限付き) ───────────
void GuildMemberProcessor::SaveCsvIfNeeded(const std::wstring& exeDir,
                                           const GuildMemberResult& result,
                                           int intervalHours) {
    if (result.guildName.empty()) return;
    if (result.members.empty()) return;

    std::lock_guard<std::mutex> lk(saveMutex_);

    // ギルド員フォルダパス
    auto folderPath = (fs::path(exeDir) / L"ログ" / L"ギルド員").wstring();

    // 初回: フォルダ内の既存ファイルをスキャンして lastSaveMap_ を復元
    if (!folderScanned_) {
        folderScanned_ = true;
        LoadLastSaveTimesFromFolder(folderPath);
    }

    // 間隔制限チェック
    auto now = result.timestamp;
    auto it = lastSaveMap_.find(result.guildName);
    if (it != lastSaveMap_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - it->second).count();
        if (elapsed < intervalHours) return;  // 間隔以内 → スキップ
    }

    // フォルダ作成
    std::error_code ec;
    fs::create_directories(folderPath, ec);

    // ファイル名: YYYYMMDD_HHMMSS_ギルド名.csv
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &tt);
    wchar_t tsBuf[32];
    swprintf_s(tsBuf, L"%04d%02d%02d_%02d%02d%02d",
               local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
               local.tm_hour, local.tm_min, local.tm_sec);

    auto filePath = (fs::path(folderPath) / (std::wstring(tsBuf) + L"_" + result.guildName + L".csv")).wstring();

    // UTF-8 BOM で書き込み
    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs.is_open()) return;

    // BOM
    ofs.write("\xEF\xBB\xBF", 3);

    // ヘッダ
    auto writeUtf8 = [&](const std::wstring& ws) {
        if (ws.empty()) return;
        int need = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
        if (need <= 0) return;
        std::string s(need, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), need, nullptr, nullptr);
        ofs.write(s.data(), s.size());
    };

    writeUtf8(L"ギルド員名,レベル,職業,役職\r\n");

    for (auto& m : result.members) {
        writeUtf8(m.name);
        ofs.write(",", 1);
        auto lvStr = std::to_string(m.level);
        ofs.write(lvStr.data(), lvStr.size());
        ofs.write(",", 1);
        writeUtf8(m.jobName);
        ofs.write(",", 1);
        writeUtf8(m.rankName);
        ofs.write("\r\n", 2);
    }

    ofs.close();

    // 保存時刻を記録
    lastSaveMap_[result.guildName] = now;
}

// ─────────── 強制CSV保存 (間隔制限無視) ───────────
void GuildMemberProcessor::ForceSaveCsv(const std::wstring& exeDir,
                                        const GuildMemberResult& result) {
    if (result.guildName.empty()) return;
    if (result.members.empty()) return;

    std::lock_guard<std::mutex> lk(saveMutex_);

    auto folderPath = (fs::path(exeDir) / L"ログ" / L"ギルド員").wstring();

    std::error_code ec;
    fs::create_directories(folderPath, ec);

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &tt);
    wchar_t tsBuf[32];
    swprintf_s(tsBuf, L"%04d%02d%02d_%02d%02d%02d",
               local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
               local.tm_hour, local.tm_min, local.tm_sec);

    auto filePath = (fs::path(folderPath) / (std::wstring(tsBuf) + L"_" + result.guildName + L".csv")).wstring();

    std::ofstream ofs(filePath, std::ios::binary);
    if (!ofs.is_open()) return;

    ofs.write("\xEF\xBB\xBF", 3);

    auto writeUtf8 = [&](const std::wstring& ws) {
        if (ws.empty()) return;
        int need = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
        if (need <= 0) return;
        std::string s(need, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), need, nullptr, nullptr);
        ofs.write(s.data(), s.size());
    };

    writeUtf8(L"ギルド員名,レベル,職業,役職\r\n");

    for (auto& m : result.members) {
        writeUtf8(m.name);
        ofs.write(",", 1);
        auto lvStr = std::to_string(m.level);
        ofs.write(lvStr.data(), lvStr.size());
        ofs.write(",", 1);
        writeUtf8(m.jobName);
        ofs.write(",", 1);
        writeUtf8(m.rankName);
        ofs.write("\r\n", 2);
    }

    ofs.close();

    // 強制保存でも保存時刻を記録 (次の自動保存の基準にする)
    lastSaveMap_[result.guildName] = now;
}
