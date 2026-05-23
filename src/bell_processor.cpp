#include "bell_processor.h"
#include <cstring>
#include <algorithm>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <thread>

// C# BellProcessor.cs の完全移植

// Restore SJIS-only decoder
static std::wstring SjisToWide(const uint8_t* data, int len) {
    if (!data || len <= 0) return std::wstring();
    int wlen = MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(data), len, nullptr, 0);
    if (wlen <= 0) return std::wstring();
    std::wstring ws(wlen, L'\0');
    MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(data), len, ws.data(), wlen);
    while (!ws.empty() && (ws.back() == L' ' || ws.back() == L'\0')) ws.pop_back();
    return ws;
}

// UTC ISO8601 文字列 <-> time_point 変換
static std::wstring ToIso8601Utc(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm gm{};
    gmtime_s(&gm, &t);
    wchar_t buf[64];
    wcsftime(buf, _countof(buf), L"%Y-%m-%dT%H:%M:%SZ", &gm);
    return std::wstring(buf);
}

static std::chrono::system_clock::time_point FromIso8601Utc(const std::wstring& s) {
    std::tm tm{};
    if (swscanf_s(s.c_str(), L"%d-%d-%dT%d:%d:%dZ",
                  &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                  &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        std::time_t tt = _mkgmtime(&tm);
        return std::chrono::system_clock::from_time_t(tt);
    }
    return std::chrono::system_clock::time_point::min();
}

BellProcessor::BellProcessor(AddChatFunc addChat, UpdateLabelFunc updateLabel,
                             SoundNotificationService& soundService, AppSettings& settings)
    : addChat_(std::move(addChat)), updateLabel_(std::move(updateLabel)),
      soundService_(soundService), settings_(settings)
{
    RestoreFromSaved();
}

BellProcessor::~BellProcessor() {
    timerRunning_ = false;
    if (timerHwnd_) KillTimer(timerHwnd_, BELL_TIMER_ID);
}

void BellProcessor::RestoreFromSaved() {
    try {
        if (!settings_.lastBellUtc.empty()) {
            auto tp = FromIso8601Utc(settings_.lastBellUtc);
            if (tp != std::chrono::system_clock::time_point::min()) {
                auto now = std::chrono::system_clock::now();
                auto elapsed = now - tp;
                auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                if (elapsedSec >= 0 && elapsedSec < 3600) {
                    remainingSeconds_ = std::max(0, 3600 - static_cast<int>(elapsedSec));
                    if (remainingSeconds_ > 0) {
                        UpdateLabel();
                        timerRunning_ = true;
                        if (timerHwnd_) SetTimer(timerHwnd_, BELL_TIMER_ID, 1000, nullptr);
                    }
                } else {
                    settings_.lastBellUtc.clear();
                    std::thread([this]() { try { settings_.Save(); } catch (...) {} }).detach();
                }
            }
        }
    } catch (...) {}
}

void BellProcessor::ProcessPacket(const uint8_t* payload, int len, bool isIncoming) {
    if (!isIncoming) return;

    try {
        if (len < 68) return;

        // マーカー検索: ペイロード内で "99 12 00 00 CC CC" を探し、
        // 見つかった位置を基準(=0)として処理する
        int base = -1;
        for (int i = 0; i + 5 < len; ++i) {
            if (payload[i] == 0x99 && payload[i+1] == 0x12 &&
                payload[i+2] == 0x00 && payload[i+3] == 0x00 &&
                payload[i+4] == 0xCC && payload[i+5] == 0xCC) {
                base = i;
                break;
            }
        }
        if (base < 0) return;

        // マーカー直前2バイトがインナーパケット長（little-endian）
        if (base < 2) return;
        uint16_t innerLen = static_cast<uint16_t>(payload[base - 2]) | (static_cast<uint16_t>(payload[base - 1]) << 8);
        int rlen = static_cast<int>(innerLen);
        // 元の長さ閾値を維持
        if (rlen < 68) return;
        // マーカー先頭(base)から rlen バイトがバッファ内に収まるか確認
        // (ユーザ要求により安全チェックを無効化) 範囲外でも処理を続ける
        // if (base + rlen > len) return;

        // 終了パケットは無視（マーカー基準の新オフセット 6-7）
        if (payload[base + 6] == 0x00 && payload[base + 7] == 0x00) return;

        // パケットレベルの重複抑止（5秒以内）
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lk(bellPacketLock_);
            auto elapsed = std::chrono::duration<double>(now - lastBellPacketTime_).count();
            if (elapsed < 5.0 && lastBellPacketTime_.time_since_epoch().count() > 0) {
                return;
            }
            lastBellPacketTime_ = now;
        }

        // マーカー基準の残り時間取得（オフセット6-7）
        if (base + 7 >= len) return;
        uint16_t remaining = static_cast<uint16_t>(payload[base + 6]) | (static_cast<uint16_t>(payload[base + 7]) << 8);

        if (remaining == 0) {
            // 終了パケットは無視（仕様どおり）
            return;
        } else if (remaining == 3600) {
            // 開始パケット: 既存の処理（通知・ログ・音あり）を使用
            HandleBellPacket(payload + base, rlen);
        } else {
            // 残り時間型パケット: 通知やログは行わず、タイマーを起動/更新して保存する
            try {
                auto nowSys = std::chrono::system_clock::now();
                // 開始時刻を逆算して保存: start_time = now - (3600 - remaining)
                auto startTp = nowSys - std::chrono::seconds(3600 - static_cast<int>(remaining));
                {
                    std::lock_guard<std::mutex> lk(lock_);
                    remainingSeconds_ = static_cast<int>(remaining);
                    // UTC時刻を保存（非同期）
                    try {
                        settings_.lastBellUtc = ToIso8601Utc(startTp);
                        std::thread([this]() { try { settings_.Save(); } catch (...) {} }).detach();
                    } catch (...) {}
                    UpdateLabel();
                    timerRunning_ = true;
                    if (timerHwnd_) SetTimer(timerHwnd_, BELL_TIMER_ID, 1000, nullptr);
                }
            } catch (...) {}
        }
    } catch (...) {}
}

void BellProcessor::HandleBellPacket(const uint8_t* payload, int len) {
    // 発動者名: 新オフセット22からNULL終端（元36->22）
    int nameStart = 22;
    int nameEnd = nameStart;
    while (nameEnd < len && payload[nameEnd] != 0) nameEnd++;
    std::wstring charName;
    if (nameEnd > nameStart) {
        charName = SjisToWide(payload + nameStart, nameEnd - nameStart);
    }
    if (charName.empty()) charName = L"不明なキャラクター";

    // メッセージ: 新オフセット40からNULL終端（元54->40）
    int msgStart = 40;
    int msgEnd = msgStart;
    while (msgEnd < len && payload[msgEnd] != 0) msgEnd++;
    std::wstring message;
    if (msgEnd > msgStart) {
        message = SjisToWide(payload + msgStart, msgEnd - msgStart);
    }

    std::wstring fullMessage = charName + L"様が鐘を鳴らしました [" + message + L"]";

    {
        std::lock_guard<std::mutex> lk(lock_);
        // 既にタイマー動作中なら前のタイマーを終了ログ扱い
        if (timerRunning_) {
            timerRunning_ = false;
            remainingSeconds_ = 0;
            EmitEndMessageIfNotDuplicate();
        }

        // タイマーを3600秒に設定
        remainingSeconds_ = 3600;
        // UTC時刻を保存
        try {
            settings_.lastBellUtc = ToIso8601Utc(std::chrono::system_clock::now());
            std::thread([this]() { try { settings_.Save(); } catch (...) {} }).detach();
        } catch (...) {}
        UpdateLabel();
        timerRunning_ = true;
        if (timerHwnd_) SetTimer(timerHwnd_, BELL_TIMER_ID, 1000, nullptr);
    }

    // チャットログ出力
    try {
        if (addChat_) addChat_(L"", fullMessage);
    } catch (...) {}

    // 音を鳴らす
    try {
        if (settings_.bellNotificationEnabled && soundService_.isBellSoundEnabled) {
            soundService_.PlayBellSound();
        }
    } catch (...) {}
}

void BellProcessor::OnTimerTick() {
    std::lock_guard<std::mutex> lk(lock_);
    if (!timerRunning_) return;

    if (remainingSeconds_ > 0) {
        remainingSeconds_--;
        UpdateLabel();
        if (remainingSeconds_ == 0) {
            timerRunning_ = false;
            if (timerHwnd_) KillTimer(timerHwnd_, BELL_TIMER_ID);
            EmitEndMessageIfNotDuplicate();
            try {
                settings_.lastBellUtc.clear();
                std::thread([this]() { try { settings_.Save(); } catch (...) {} }).detach();
            } catch (...) {}
        }
    }
}

void BellProcessor::EmitEndMessageIfNotDuplicate() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - lastEndLogTime_).count();
    if (elapsed < 1.0 && lastEndLogTime_.time_since_epoch().count() > 0) return;
    lastEndLogTime_ = now;
    try {
        if (addChat_) addChat_(L"システム", L"導きの鐘の効果が終了しました");
    } catch (...) {}
}

void BellProcessor::UpdateLabel() {
    int sec = std::max(0, remainingSeconds_);
    int minutes = sec / 60;
    int seconds = sec % 60;
    wchar_t buf[64];
    swprintf_s(buf, L"鐘タイマー: %02d:%02d", minutes, seconds);
    try {
        if (updateLabel_) updateLabel_(buf);
    } catch (...) {}
}
