#pragma once
#include "app_settings.h"
#include "sound_notification_service.h"
#include <string>
#include <cstdint>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// C# BellProcessor.cs の完全移植
// 導きの鐘パケットの判定・タイマー管理・ログ通知

class BellProcessor {
public:
    using AddChatFunc = std::function<void(const std::wstring& sender, const std::wstring& message)>;
    using UpdateLabelFunc = std::function<void(const std::wstring& text)>;

    BellProcessor(AddChatFunc addChat, UpdateLabelFunc updateLabel,
                  SoundNotificationService& soundService, AppSettings& settings);
    ~BellProcessor();

    // パケット処理（キャプチャスレッドから呼ばれる）
    void ProcessPacket(const uint8_t* payload, int len, bool isIncoming);

    // Win32タイマーのコールバック（WM_TIMER経由で呼ぶ）
    void OnTimerTick();

    // 起動時の復元（コンストラクタ内でも呼ぶ）
    void RestoreFromSaved();

    // main側でHWND設定（SetTimer/KillTimer用）
    void SetTimerHwnd(HWND hwnd) { timerHwnd_ = hwnd; }

    int GetRemainingSeconds() const { return remainingSeconds_; }

    // Win32タイマーID
    static constexpr UINT_PTR BELL_TIMER_ID = 9001;

private:
    void HandleBellPacket(const uint8_t* payload, int len);
    void EmitEndMessageIfNotDuplicate();
    void UpdateLabel();

    AddChatFunc addChat_;
    UpdateLabelFunc updateLabel_;
    SoundNotificationService& soundService_;
    AppSettings& settings_;

    std::mutex lock_;
    int remainingSeconds_ = 0;
    bool timerRunning_ = false;
    HWND timerHwnd_ = nullptr; // SetTimer用

    // 終了ログの重複抑止
    std::chrono::steady_clock::time_point lastEndLogTime_{};

    // パケットレベルの重複抑止
    std::mutex bellPacketLock_;
    std::chrono::steady_clock::time_point lastBellPacketTime_{};
};
