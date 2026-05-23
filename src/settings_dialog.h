#pragma once
#include "app_settings.h"
#include "network_helper.h"
#include "process_monitor_service.h"
#include "sound_notification_service.h"
#include <string>
#include <vector>
#include <functional>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

// C# SettingsDialog.cs の完全移植
// Win32 ダイアログ形式（3タブ: ネットワーク / 通知音 / 表示タブ）

class SettingsDialog {
public:
    using SystemLogFunc = std::function<void(const std::wstring&)>;

    SettingsDialog(AppSettings& settings,
                   ProcessMonitorService& processMonitor,
                   SoundNotificationService& soundService,
                   SystemLogFunc addSystemLog,
                   const std::vector<std::wstring>& availableTabs = {},
                   const std::vector<std::wstring>& currentSelectedTabs = {});

    // モーダルダイアログとして表示（閉じるまでブロック）
    void Show(HWND parent);

    // 表示タブの更新後の結果を取得
    std::vector<std::wstring> GetSelectedTabs() const { return currentSelectedTabs_; }

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    INT_PTR HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void InitDialog(HWND hwnd);
    void CreateNetworkTab(HWND tabCtrl, HWND parent);
    void CreateSoundTab(HWND tabCtrl, HWND parent);
    void CreateDisplayTab(HWND tabCtrl, HWND parent);
    void OnTabChanged(HWND hwnd, int tabIndex);

    void SaveNetworkSettings(HWND hwnd);
    void SaveSoundSettings(HWND hwnd);
    void SaveTabSettings(HWND hwnd);

    // PID Picker
    void StartPidPicker(HWND hwnd);
    void StopPidPicker(HWND hwnd);
    void PollMouseClick(HWND hwnd);
    void OnPidPicked(HWND hwnd, int pid);
    void UpdatePidDisplay(HWND hwnd);

    AppSettings& settings_;
    ProcessMonitorService& processMonitor_;
    SoundNotificationService& soundService_;
    SystemLogFunc addSystemLog_;
    std::vector<std::wstring> availableTabs_;
    std::vector<std::wstring> currentSelectedTabs_;

    std::vector<NetworkInterfaceInfo> networkInterfaces_;

    // コントロールID
    enum {
        IDC_TAB = 1001,
        IDC_IFACE_COMBO = 1010,
        IDC_SAVE_NETWORK = 1011,
        IDC_PID_PICKER = 1012,
        IDC_PID_STOP = 1013,
        IDC_PID_STATUS = 1014,
        IDC_PID_PORTS = 1015,
        IDC_PID_IP = 1016,
        IDC_CHK_WHISPER = 1020,
        IDC_CHK_ITEM = 1021,
        IDC_CHK_OPTION = 1022,
        IDC_CHK_INVFULL = 1023,
        IDC_CHK_BELL = 1024,
        IDC_VOLUME = 1025,
        IDC_SAVE_SOUND = 1026,
        IDC_CHK_DROP = 1027,
        IDC_TAB_CHECK_BASE = 1100, // 1100+i
        IDC_SAVE_TABS = 1150,
        IDC_CLOSE = 1200,
        IDC_PID_TIMER = 9002,
    };

    // タブページのHWND
    HWND networkPage_ = nullptr;
    HWND soundPage_ = nullptr;
    HWND displayPage_ = nullptr;

    // PID Picker 状態
    bool pidPickerActive_ = false;
    bool pidPickerLastState_ = false;
};
