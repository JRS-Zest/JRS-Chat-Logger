// gold_processor.cpp — GoldProcessor: 収入タブのパケット解析
// C# GoldProcessor.cs の C++ 移植
#include "gold_processor.h"
#include "app_settings.h"

#include <windows.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "advapi32.lib")

namespace fs = std::filesystem;

GoldProcessor::GoldProcessor() = default;

void GoldProcessor::StartCollection() {
    // 全カウンタが0のときだけ開始時刻を保存する（途中再開では上書きしない）
    bool shouldSetStart = false;
    {
        std::lock_guard<std::mutex> lk(lock_);
        if (pickupGoldTotal_ == 0 && merchantGoldTotal_ == 0 && mobGoldTotal_ == 0 &&
            mobKillCount_ == 0 && decomposeCount_ == 0 && tatisFragmentCount_ == 0 &&
            mysteryFragmentCount_ == 0 && crystalStoneCount_ == 0 &&
            adventurerCoinCount_ == 0 && umuCoinCount_ == 0)
        {
            shouldSetStart = true;
        }
    }

    collecting_ = true;

    if (shouldSetStart && settings_) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        struct tm tmv{};
        gmtime_s(&tmv, &t);
        wchar_t buf[64];
        swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        settings_->incomeStartUtc = buf;
        settings_->Save();
    }
}

void GoldProcessor::StopCollection() {
    collecting_ = false;
}

void GoldProcessor::ResetCollection() {
    {
        std::lock_guard<std::mutex> lk(lock_);
        pickupGoldTotal_ = 0;
        merchantGoldTotal_ = 0;
        mobGoldTotal_ = 0;
        mobKillCount_ = 0;
        decomposeCount_ = 0;
        tatisFragmentCount_ = 0;
        mysteryFragmentCount_ = 0;
        crystalStoneCount_ = 0;
        adventurerCoinCount_ = 0;
        umuCoinCount_ = 0;
    }
    NotifyUpdate();

    // リセット後、集計中なら開始時刻を「今」に更新して保存
    if (collecting_.load() && settings_) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        struct tm tmv{};
        gmtime_s(&tmv, &t);
        wchar_t buf[64];
        swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        settings_->incomeStartUtc = buf;
        settings_->Save();
    }
}

GoldStats GoldProcessor::GetStats() const {
    std::lock_guard<std::mutex> lk(lock_);
    GoldStats s;
    s.pickupGoldTotal = pickupGoldTotal_;
    s.merchantGoldTotal = merchantGoldTotal_;
    s.mobGoldTotal = mobGoldTotal_;
    s.mobKillCount = mobKillCount_;
    s.decomposeCount = decomposeCount_;
    s.crystalStoneCount = crystalStoneCount_;
    s.umuCoinCount = umuCoinCount_;
    return s;
}

void GoldProcessor::ProcessPacket(const uint8_t* data, int len, bool /*isIncoming*/, int /*srcPort*/, int /*dstPort*/) {
    if (!collecting_.load()) return;
    if (!data || len < 16) return;

    uint16_t cmdId;
    std::memcpy(&cmdId, data + 2, 2);
    if (cmdId != CMD_ID) return;

    uint16_t subCmd = 0;
    if (len >= 16) {
        std::memcpy(&subCmd, data + 14, 2);
    }

    // 拾得ゴールドパケット
    if (subCmd == SUB_CMD_PICKUP) {
        AnalyzePickupGoldPacket(data, len);
    } else if (IsCombinedPickupGoldPacket(data, len)) {
        ProcessCombinedPickupGoldPacket(data, len);
    }

    // Mob討伐パケット判定・処理
    if (IsMobStatsPacket(data, len)) {
        ProcessMobStatsPacket(data, len);
    }

    // 行商販売パケット処理
    ProcessCombinedMerchantSalesPacket(data, len);

    // U分解パケット処理
    ProcessDecomposePacket(data, len);
}

// ───────── 拾得ゴールド (Combined) ─────────
bool GoldProcessor::IsCombinedPickupGoldPacket(const uint8_t* data, int len) {
    if (len < 20) return false;
    uint16_t cmdId;
    std::memcpy(&cmdId, data + 2, 2);
    if (cmdId != CMD_ID) return false;

    // 0x38, 0x11 マーカーを探す
    bool foundPickupMarker = false;
    for (int i = 12; i <= len - 2; i++) {
        if (i == 14) continue;
        if (data[i] == 0x38 && data[i + 1] == 0x11) {
            foundPickupMarker = true;
            break;
        }
    }
    if (!foundPickupMarker) return false;

    // 00 00 00 00 FF FF 必須パターン
    const uint8_t required[] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF};
    for (int i = 0; i <= len - (int)sizeof(required); i++) {
        bool ok = true;
        for (int j = 0; j < (int)sizeof(required); j++) {
            if (data[i + j] != required[j]) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

void GoldProcessor::ProcessCombinedPickupGoldPacket(const uint8_t* data, int len) {
    if (!collecting_.load()) return;

    auto current = NowMs();
    auto dataHash = ComputeMD5(data, len);
    bool isSame;
    {
        std::lock_guard<std::mutex> lk(dupLock_);
        isSame = (lastHash_ == dataHash);
    }
    bool withinTime = (current - lastTimeMs_) < 300;
    if (isSame && withinTime) return;
    {
        std::lock_guard<std::mutex> lk(dupLock_);
        lastHash_ = dataHash;
        lastTimeMs_ = current;
    }

    const uint8_t required[] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF};
    for (int i = 12; i <= len - 18; i++) {
        if (data[i] == 0x38 && data[i + 1] == 0x11) {
            if (i + 18 > len) continue;
            bool match = true;
            for (int k = 0; k < (int)sizeof(required); k++) {
                if (data[i + 12 + k] != required[k]) { match = false; break; }
            }
            if (!match) continue;
            if (i + 6 + 4 > len) continue;
            uint32_t pickupGold;
            std::memcpy(&pickupGold, data + i + 6, 4);
            if (pickupGold == 0 || pickupGold > 100000) continue;
            {
                std::lock_guard<std::mutex> lk(lock_);
                pickupGoldTotal_ += pickupGold;
            }
            NotifyUpdate();
        }
    }
}

void GoldProcessor::AnalyzePickupGoldPacket(const uint8_t* data, int len) {
    if (!collecting_.load()) return;
    if (len < 24) return;
    uint32_t pickupGold;
    std::memcpy(&pickupGold, data + 20, 4);
    if (pickupGold == 0 || pickupGold > 100000) return;
    {
        std::lock_guard<std::mutex> lk(lock_);
        pickupGoldTotal_ += pickupGold;
    }
    NotifyUpdate();
}

// ───────── 行商販売 ─────────
void GoldProcessor::ProcessCombinedMerchantSalesPacket(const uint8_t* data, int len) {
    if (!collecting_.load()) return;
    uint32_t totalSales = 0;
    const uint8_t marker[] = {0x62, 0x11};
    int searchStart = 4;
    while (searchStart < len) {
        int idx = IndexOf(data, len, marker, 2, searchStart);
        if (idx == -1) break;
        int goldStart = idx + 10;
        if (goldStart + 4 <= len) {
            uint32_t sale;
            std::memcpy(&sale, data + goldStart, 4);
            if (sale <= 100000) {
                totalSales += sale;
            } else {
                searchStart = idx + 2;
                continue;
            }
        } else break;
        searchStart = idx + 2;
    }
    if (totalSales > 0) {
        std::lock_guard<std::mutex> lk(lock_);
        merchantGoldTotal_ += totalSales;
    }
    if (totalSales > 0) NotifyUpdate();
}

// ───────── Mob討伐 ─────────
bool GoldProcessor::IsMobStatsPacket(const uint8_t* data, int len) {
    if (len < 16) return false;
    uint16_t cmdId;
    std::memcpy(&cmdId, data + 2, 2);
    if (cmdId != CMD_ID) return false;

    const uint8_t a311Header[] = {0xA3, 0x11, 0x00, 0x00, 0xCC, 0xCC};
    for (int i = 4; i <= len - (int)sizeof(a311Header); i++) {
        bool ok = true;
        for (int j = 0; j < (int)sizeof(a311Header); j++) {
            if (data[i + j] != a311Header[j]) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

void GoldProcessor::ProcessMobStatsPacket(const uint8_t* data, int len) {
    if (!collecting_.load()) return;

    const uint8_t a311Header[] = {0xA3, 0x11, 0x00, 0x00, 0xCC, 0xCC};
    std::set<int> usedA311;

    for (int i = 4; i <= len - (int)sizeof(a311Header); i++) {
        bool match = true;
        for (int j = 0; j < (int)sizeof(a311Header); j++) {
            if (data[i + j] != a311Header[j]) { match = false; break; }
        }
        if (!match) continue;
        if (usedA311.count(i)) continue;

        int goldOffset = i + (int)sizeof(a311Header);
        if (goldOffset + 1 >= len) continue;

        uint16_t gold;
        std::memcpy(&gold, data + goldOffset, 2);
        if (gold > 0 && gold <= 100000) {
            {
                std::lock_guard<std::mutex> lk(lock_);
                mobGoldTotal_ += gold;
            }
            usedA311.insert(i);
        }
    }

    int killCount = (int)usedA311.size();
    if (killCount > 0) {
        std::lock_guard<std::mutex> lk(lock_);
        mobKillCount_ += killCount;
    }
    if (killCount > 0) NotifyUpdate();
}

// ───────── U分解 ─────────
void GoldProcessor::ProcessDecomposePacket(const uint8_t* data, int len) {
    if (!collecting_.load()) return;
    if (len < 4) return;
    uint16_t cmdId;
    std::memcpy(&cmdId, data + 2, 2);
    if (cmdId != CMD_ID) return;

    const uint8_t subMarker[] = {0x5d, 0x12};
    const uint8_t e400[] = {0xe4, 0x00};
    int searchStart = 4;
    int totalTatis = 0, totalMystery = 0, totalCrystal = 0;
    int decomposeEvents = 0;

    while (searchStart < len) {
        int idx = IndexOf(data, len, subMarker, 2, searchStart);
        if (idx == -1) break;
        int e400Index = idx + 4;
        if (e400Index + (int)sizeof(e400) <= len && data[e400Index] == e400[0] && data[e400Index + 1] == e400[1]) {
            decomposeEvents++;
            int tatisOffset = e400Index + 6;
            int mysteryOffset = e400Index + 10;
            int crystalOffset = e400Index + 14;

            if (crystalOffset + 2 <= len) {
                uint16_t tatis, mystery, crystal;
                std::memcpy(&tatis, data + tatisOffset, 2);
                std::memcpy(&mystery, data + mysteryOffset, 2);
                std::memcpy(&crystal, data + crystalOffset, 2);
                totalTatis += tatis;
                totalMystery += mystery;
                totalCrystal += crystal;
            }
        }
        searchStart = idx + 2;
    }

    if (decomposeEvents > 0) {
        std::lock_guard<std::mutex> lk(lock_);
        decomposeCount_ += decomposeEvents;
        tatisFragmentCount_ += totalTatis;
        mysteryFragmentCount_ += totalMystery;
        crystalStoneCount_ += totalCrystal;
        adventurerCoinCount_ += totalTatis;  // サーバ固有
        umuCoinCount_ += totalMystery;       // サーバ固有
    }
    if (decomposeEvents > 0) NotifyUpdate();
}

// ───────── ログ保存 ─────────
void GoldProcessor::SaveIncomeLog() {
    try {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        fs::path baseDir = fs::path(exePath).parent_path();

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        struct tm tmv{};
        localtime_s(&tmv, &t);

        wchar_t monthFolder[64];
        swprintf_s(monthFolder, L"%04d年%02d月分", tmv.tm_year + 1900, tmv.tm_mon + 1);

        fs::path dir = baseDir / L"ログ" / L"収入" / monthFolder;
        fs::create_directories(dir);

        wchar_t fileName[128];
        swprintf_s(fileName, L"%04d_%02d_%02d_%02d_%02d_%02d_income.log",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

        fs::path filePath = dir / fileName;

        std::wostringstream sb;

        // 開始時刻（UTC→ローカルタイムに変換して日本語形式で出力）
        if (settings_ && !settings_->incomeStartUtc.empty()) {
            // ISO8601 "YYYY-MM-DDTHH:MM:SSZ" をパースしてローカルタイムへ変換
            struct tm startTm{};
            int sy, smo, sd, sh, smi, ss;
            if (swscanf_s(settings_->incomeStartUtc.c_str(),
                          L"%d-%d-%dT%d:%d:%dZ", &sy, &smo, &sd, &sh, &smi, &ss) == 6) {
                startTm.tm_year = sy - 1900; startTm.tm_mon = smo - 1; startTm.tm_mday = sd;
                startTm.tm_hour = sh; startTm.tm_min = smi; startTm.tm_sec = ss;
                startTm.tm_isdst = -1;
                time_t utcTime = _mkgmtime(&startTm);
                struct tm localTm{};
                localtime_s(&localTm, &utcTime);
                wchar_t startBuf[64];
                swprintf_s(startBuf, L"%04d年%02d月%02d日%02d:%02d_%02d",
                    localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday,
                    localTm.tm_hour, localTm.tm_min, localTm.tm_sec);
                sb << L"開始時刻: " << startBuf << L"\r\n";
            } else {
                sb << L"開始時刻: " << settings_->incomeStartUtc << L"\r\n";
            }
        } else {
            sb << L"開始時刻: 不明\r\n";
        }

        // 終了時刻
        wchar_t endTime[64];
        swprintf_s(endTime, L"%04d年%02d月%02d日%02d:%02d_%02d",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        sb << L"終了時刻: " << endTime << L"\r\n";

        {
            std::lock_guard<std::mutex> lk(lock_);
            sb << L"倒したMob数: " << mobKillCount_ << L"体\r\n";
            sb << L"収入\r\n";
            sb << L"Mob: " << mobGoldTotal_ << L"Gold\r\n";
            sb << L"拾得: " << pickupGoldTotal_ << L"Gold\r\n";
            sb << L"行商: " << merchantGoldTotal_ << L"Gold\r\n";
            sb << L"合計: " << (mobGoldTotal_ + pickupGoldTotal_ + merchantGoldTotal_) << L"Gold\r\n";
            sb << L"U分解\r\n";
            sb << L"分解数: " << decomposeCount_ << L"個\r\n";
            sb << L"結晶石: " << crystalStoneCount_ << L"個\r\n";
            sb << L"UMUコイン: " << umuCoinCount_ << L"個\r\n";
        }

        // UTF-8 BOM付きで書き出し
        std::wstring content = sb.str();
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), (int)content.size(), nullptr, 0, nullptr, nullptr);
        std::string utf8(utf8Len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, content.c_str(), (int)content.size(), utf8.data(), utf8Len, nullptr, nullptr);

        std::ofstream ofs(filePath, std::ios::binary);
        // UTF-8 BOM
        const char bom[] = {'\xEF', '\xBB', '\xBF'};
        ofs.write(bom, 3);
        ofs.write(utf8.data(), utf8.size());
        ofs.close();

        // 保存成功ダイアログ
        std::wstring dirStr = dir.wstring();
        std::wstring msg = L"保存しました:\n" + dirStr + L"\nフォルダを開きますか？";
        int result = MessageBoxW(nullptr, msg.c_str(), L"収入ログ", MB_YESNO | MB_ICONINFORMATION);
        if (result == IDYES) {
            ShellExecuteW(nullptr, L"open", dirStr.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    } catch (...) {
        MessageBoxW(nullptr, L"ログ保存に失敗しました。", L"収入ログ", MB_OK | MB_ICONWARNING);
    }
}

// ───────── ヘルパー ─────────
int GoldProcessor::IndexOf(const uint8_t* haystack, int hayLen, const uint8_t* needle, int needleLen, int start) {
    for (int i = start; i <= hayLen - needleLen; i++) {
        bool ok = true;
        for (int j = 0; j < needleLen; j++) {
            if (haystack[i + j] != needle[j]) { ok = false; break; }
        }
        if (ok) return i;
    }
    return -1;
}

std::string GoldProcessor::ComputeMD5(const uint8_t* data, int len) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::string result;

    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return result;
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) { CryptReleaseContext(hProv, 0); return result; }
    CryptHashData(hHash, data, len, 0);

    DWORD hashLen = 16;
    uint8_t hashBytes[16];
    CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashLen, 0);

    char hex[33];
    for (int i = 0; i < 16; i++) {
        sprintf_s(hex + i * 2, 3, "%02X", hashBytes[i]);
    }
    result.assign(hex, 32);

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return result;
}

uint64_t GoldProcessor::NowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void GoldProcessor::NotifyUpdate() {
    guiUpdatePending_.store(1);
    if (updateCallback_) {
        try { updateCallback_(); } catch (...) {}
    }
}
