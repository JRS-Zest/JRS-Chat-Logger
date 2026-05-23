#include "settings_dialog.h"
#include "resource.h"
#include "theme_manager.h"
#include <algorithm>
#include <sstream>

#pragma comment(lib, "comctl32.lib")

// C# SettingsDialog.cs の完全移植 (Win32 ダイアログ版)

SettingsDialog::SettingsDialog(AppSettings& settings,
                               ProcessMonitorService& processMonitor,
                               SoundNotificationService& soundService,
                               SystemLogFunc addSystemLog,
                               const std::vector<std::wstring>& availableTabs,
                               const std::vector<std::wstring>& currentSelectedTabs)
    : settings_(settings), processMonitor_(processMonitor), soundService_(soundService),
      addSystemLog_(std::move(addSystemLog)), availableTabs_(availableTabs),
      currentSelectedTabs_(currentSelectedTabs)
{
}

void SettingsDialog::Show(HWND parent) {
    // モーダルダイアログ テンプレート作成
    alignas(4) BYTE buf[512] = {};
    auto* dt = reinterpret_cast<DLGTEMPLATE*>(buf);
    dt->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    dt->cdit = 0;
    dt->cx = 300; dt->cy = 290;
    auto* p = reinterpret_cast<WORD*>(dt + 1);
    *p++ = 0; *p++ = 0;
    const wchar_t title[] = L"設定";
    memcpy(p, title, sizeof(title));

    DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        dt,
        parent,
        DialogProc,
        reinterpret_cast<LPARAM>(this)
    );
}

INT_PTR CALLBACK SettingsDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialog* self;
    if (msg == WM_INITDIALOG) {
        self = reinterpret_cast<SettingsDialog*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);
    return FALSE;
}

INT_PTR SettingsDialog::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        {
            HICON hIco = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON));
            if (hIco) { SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIco); SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco); }
        }
        InitDialog(hwnd);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SAVE_NETWORK: SaveNetworkSettings(hwnd); return TRUE;
        case IDC_SAVE_SOUND:   SaveSoundSettings(hwnd); return TRUE;
        case IDC_SAVE_TABS:    SaveTabSettings(hwnd); return TRUE;
        case IDC_PID_PICKER:   StartPidPicker(hwnd); return TRUE;
        case IDC_PID_STOP:
            processMonitor_.StopMonitoring();
            if (addSystemLog_) addSystemLog_(L"PID監視を停止しました");
            UpdatePidDisplay(hwnd);
            return TRUE;
        case IDC_CLOSE:
        case IDCANCEL:
            StopPidPicker(hwnd);
            EndDialog(hwnd, 0);
            return TRUE;
        }
        break;

    case WM_NOTIFY: {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr->idFrom == IDC_TAB && nmhdr->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(nmhdr->hwndFrom);
            OnTabChanged(hwnd, sel);
        }
        return TRUE;
    }

    case WM_TIMER:
        if (wParam == IDC_PID_TIMER) {
            PollMouseClick(hwnd);
            return TRUE;
        }
        break;

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlType == ODT_BUTTON) {
            ThemeManager::DrawStyledButton(dis);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        StopPidPicker(hwnd);
        EndDialog(hwnd, 0);
        return TRUE;
    }
    return FALSE;
}

void SettingsDialog::InitDialog(HWND hwnd) {
    // ダイアログサイズ設定
    RECT rc = {0, 0, 450, 555};
    AdjustWindowRect(&rc, GetWindowLongW(hwnd, GWL_STYLE), FALSE);
    SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    CenterWindowOnParent(hwnd, GetParent(hwnd));

    // フォント
    HFONT hFont = ThemeManager::CreateDefaultFont();
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);

    // タブコントロール
    HWND tabCtrl = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        5, 5, 440, 500, hwnd, (HMENU)IDC_TAB,
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(tabCtrl, WM_SETFONT, (WPARAM)hFont, TRUE);

    TCITEMW ti = {};
    ti.mask = TCIF_TEXT;
    ti.pszText = const_cast<wchar_t*>(L"ネットワーク");
    TabCtrl_InsertItem(tabCtrl, 0, &ti);
    ti.pszText = const_cast<wchar_t*>(L"通知音");
    TabCtrl_InsertItem(tabCtrl, 1, &ti);
    ti.pszText = const_cast<wchar_t*>(L"表示タブ");
    TabCtrl_InsertItem(tabCtrl, 2, &ti);

    // 各タブページ作成
    CreateNetworkTab(tabCtrl, hwnd);
    CreateSoundTab(tabCtrl, hwnd);
    CreateDisplayTab(tabCtrl, hwnd);

    // 閉じるボタン（角丸スタイル）
    ThemeManager::CreateStyledButton(L"閉じる", 175, 515, 100, 30, hwnd, IDC_CLOSE);

    // 全子ウィンドウにフォント適用
    ThemeManager::ApplyFontToChildren(hwnd, hFont);

    // 初期タブ表示
    OnTabChanged(hwnd, 0);
}

static LRESULT CALLBACK TabPageSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
    // ボタンクリック等の WM_COMMAND / WM_DRAWITEM を親ダイアログに転送
    if (msg == WM_COMMAND || msg == WM_DRAWITEM) {
        HWND dlg = GetParent(hwnd);
        if (dlg) return SendMessageW(dlg, msg, wParam, lParam);
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, TabPageSubclassProc, 0);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static HWND CreateTabPage(HWND parent, HWND tabCtrl) {
    RECT rc;
    TabCtrl_GetItemRect(tabCtrl, 0, &rc);
    int tabH = rc.bottom - rc.top + 4;
    RECT tabRc;
    GetClientRect(tabCtrl, &tabRc);
    HWND page = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_CLIPCHILDREN,
        tabRc.left + 5, tabH + 5,
        tabRc.right - tabRc.left - 10,
        tabRc.bottom - tabH - 10,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowSubclass(page, TabPageSubclassProc, 0, 0);
    return page;
}

void SettingsDialog::CreateNetworkTab(HWND tabCtrl, HWND parent) {
    networkPage_ = CreateTabPage(parent, tabCtrl);
    HFONT hFont = ThemeManager::CreateDefaultFont();
    int y = 5;

    CreateWindowExW(0, L"STATIC", L"ネットワークカード設定",
        WS_CHILD | WS_VISIBLE, 10, y, 300, 20, networkPage_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    y += 25;

    CreateWindowExW(0, L"STATIC",
        L"パケットキャプチャに使用するインターフェースを選択してください。\n通常はデフォルトゲートウェイが設定されているインターフェースが自動選択されます。",
        WS_CHILD | WS_VISIBLE, 10, y, 400, 55, networkPage_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    y += 60;

    auto combo = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        10, y, 380, 200, networkPage_, (HMENU)IDC_IFACE_COMBO,
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(combo, WM_SETFONT, (WPARAM)hFont, TRUE);
    y += 35;

    // インターフェース一覧
    networkInterfaces_ = NetworkHelper::GetNetworkInterfaces();
    auto defaultIface = NetworkHelper::GetDefaultGatewayInterfaceName();
    int selectedIdx = -1;

    for (size_t i = 0; i < networkInterfaces_.size(); i++) {
        auto& iface = networkInterfaces_[i];
        auto text = iface.name + L" (" + iface.ipAddress + L")";
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
        if (!settings_.interfaceName.empty() && iface.name == settings_.interfaceName) {
            selectedIdx = static_cast<int>(i);
        } else if (selectedIdx < 0 && !defaultIface.empty() && iface.name == defaultIface) {
            selectedIdx = static_cast<int>(i);
        }
    }
    if (selectedIdx >= 0) SendMessageW(combo, CB_SETCURSEL, selectedIdx, 0);
    else if (!networkInterfaces_.empty()) SendMessageW(combo, CB_SETCURSEL, 0, 0);

    ThemeManager::CreateStyledButton(L"ネットワークカード設定を保存", 10, y, 220, 30, networkPage_, IDC_SAVE_NETWORK);
    y += 50;

    // 区切り線
    CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        10, y, 390, 2, networkPage_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    y += 10;

    // PID Picker セクション
    CreateWindowExW(0, L"STATIC", L"PID 固定監視設定",
        WS_CHILD | WS_VISIBLE, 10, y, 300, 20, networkPage_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    y += 25;

    ThemeManager::CreateStyledButton(L"ゲーム画面をクリックしてウィンドウ情報を取得", 10, y, 340, 30, networkPage_, IDC_PID_PICKER);
    y += 35;

    ThemeManager::CreateStyledButton(L"監視停止", 10, y, 100, 28, networkPage_, IDC_PID_STOP);
    y += 35;

    CreateWindowExW(0, L"STATIC", L"未監視",
        WS_CHILD | WS_VISIBLE, 10, y, 300, 20, networkPage_, (HMENU)IDC_PID_STATUS,
        GetModuleHandleW(nullptr), nullptr);
    y += 22;

    CreateWindowExW(0, L"STATIC", L"検出ポート: なし",
        WS_CHILD | WS_VISIBLE, 10, y, 300, 20, networkPage_, (HMENU)IDC_PID_PORTS,
        GetModuleHandleW(nullptr), nullptr);
    y += 22;

    CreateWindowExW(0, L"STATIC", L"検出IP: なし",
        WS_CHILD | WS_VISIBLE, 10, y, 300, 20, networkPage_, (HMENU)IDC_PID_IP,
        GetModuleHandleW(nullptr), nullptr);

    UpdatePidDisplay(networkPage_);
    ThemeManager::ApplyFontToChildren(networkPage_, ThemeManager::CreateDefaultFont());
}

void SettingsDialog::CreateSoundTab(HWND tabCtrl, HWND parent) {
    soundPage_ = CreateTabPage(parent, tabCtrl);
    int y = 5;

    CreateWindowExW(0, L"STATIC", L"通知音設定",
        WS_CHILD | WS_VISIBLE, 10, y, 300, 20, soundPage_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    y += 30;

    struct SoundItem { int id; const wchar_t* label; bool checked; };
    SoundItem items[] = {
        { IDC_CHK_WHISPER, L"ささやき受信 (whisper.wav)", settings_.whisperNotificationEnabled },
        { IDC_CHK_ITEM,    L"アイテム拾得 (item.wav)",    settings_.itemNotificationEnabled },
        { IDC_CHK_OPTION,  L"オプション名通知 (option.wav)", settings_.optionNotificationEnabled },
        { IDC_CHK_INVFULL, L"インベントリ満杯 (inv_full.wav)", settings_.invFullNotificationEnabled },
        { IDC_CHK_BELL,    L"導きの鐘 (bell.wav)",        settings_.bellNotificationEnabled },
        { IDC_CHK_DROP,    L"ドロップ (Ultimate.wav)",    settings_.dropNotificationEnabled },
    };

    for (auto& item : items) {
        auto btn = CreateWindowExW(0, L"BUTTON", item.label,
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            20, y, 350, 20, soundPage_, (HMENU)(intptr_t)item.id,
            GetModuleHandleW(nullptr), nullptr);
        if (item.checked) SendMessageW(btn, BM_SETCHECK, BST_CHECKED, 0);
        y += 25;
    }

    y += 10;
    CreateWindowExW(0, L"STATIC", L"全体音量",
        WS_CHILD | WS_VISIBLE, 10, y, 100, 20, soundPage_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    y += 25;

    auto trackbar = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
        10, y, 350, 30, soundPage_, (HMENU)IDC_VOLUME,
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(trackbar, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(trackbar, TBM_SETPOS, TRUE, static_cast<int>(settings_.soundVolume * 100));
    SendMessageW(trackbar, TBM_SETTICFREQ, 10, 0);
    y += 40;

    ThemeManager::CreateStyledButton(L"サウンド設定を保存", 10, y, 160, 30, soundPage_, IDC_SAVE_SOUND);
    ThemeManager::ApplyFontToChildren(soundPage_, ThemeManager::CreateDefaultFont());
}

void SettingsDialog::CreateDisplayTab(HWND tabCtrl, HWND parent) {
    displayPage_ = CreateTabPage(parent, tabCtrl);
    int y = 5;

    CreateWindowExW(0, L"STATIC", L"表示タブ設定",
        WS_CHILD | WS_VISIBLE, 10, y, 300, 20, displayPage_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    y += 30;

    CreateWindowExW(0, L"STATIC", L"表示したいタブにチェックを入れてください。",
        WS_CHILD | WS_VISIBLE, 10, y, 380, 20, displayPage_, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    y += 30;

    for (size_t i = 0; i < availableTabs_.size(); i++) {
        auto& name = availableTabs_[i];
        if (name == L"チャット") {
            CreateWindowExW(0, L"STATIC", L"チャット（非表示不可）",
                WS_CHILD | WS_VISIBLE, 20, y + 4, 200, 20, displayPage_, nullptr,
                GetModuleHandleW(nullptr), nullptr);
            y += 28;
            continue;
        }
        bool checked = std::find(currentSelectedTabs_.begin(), currentSelectedTabs_.end(), name)
                        != currentSelectedTabs_.end();
        auto btn = CreateWindowExW(0, L"BUTTON", name.c_str(),
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            20, y, 300, 20, displayPage_, (HMENU)(intptr_t)(IDC_TAB_CHECK_BASE + i),
            GetModuleHandleW(nullptr), nullptr);
        if (checked) SendMessageW(btn, BM_SETCHECK, BST_CHECKED, 0);
        y += 28;
    }

    y += 10;
    ThemeManager::CreateStyledButton(L"タブ設定を保存", 10, y, 140, 30, displayPage_, IDC_SAVE_TABS);
    ThemeManager::ApplyFontToChildren(displayPage_, ThemeManager::CreateDefaultFont());
}

void SettingsDialog::OnTabChanged(HWND /*hwnd*/, int tabIndex) {
    ShowWindow(networkPage_, tabIndex == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(soundPage_, tabIndex == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(displayPage_, tabIndex == 2 ? SW_SHOW : SW_HIDE);
}

void SettingsDialog::SaveNetworkSettings(HWND /*hwnd*/) {
    auto combo = GetDlgItem(networkPage_, IDC_IFACE_COMBO);
    int sel = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < static_cast<int>(networkInterfaces_.size())) {
        settings_.interfaceName = networkInterfaces_[sel].name;
        settings_.Save();
        if (addSystemLog_) {
            addSystemLog_(L"ネットワークインターフェースを '" + networkInterfaces_[sel].name + L"' に設定しました。");
        }
    }
}

void SettingsDialog::SaveSoundSettings(HWND /*hwnd*/) {
    auto getCheck = [&](int id) -> bool {
        auto ctrl = GetDlgItem(soundPage_, id);
        return SendMessageW(ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED;
    };

    settings_.whisperNotificationEnabled = getCheck(IDC_CHK_WHISPER);
    settings_.itemNotificationEnabled = getCheck(IDC_CHK_ITEM);
    settings_.optionNotificationEnabled = getCheck(IDC_CHK_OPTION);
    settings_.invFullNotificationEnabled = getCheck(IDC_CHK_INVFULL);
    settings_.bellNotificationEnabled = getCheck(IDC_CHK_BELL);
    settings_.dropNotificationEnabled = getCheck(IDC_CHK_DROP);

    auto trackbar = GetDlgItem(soundPage_, IDC_VOLUME);
    int vol = static_cast<int>(SendMessageW(trackbar, TBM_GETPOS, 0, 0));
    settings_.soundVolume = vol / 100.0f;

    // SoundServiceにも反映
    soundService_.isWhisperSoundEnabled = settings_.whisperNotificationEnabled;
    soundService_.isItemSoundEnabled = settings_.itemNotificationEnabled;
    soundService_.isOptionSoundEnabled = settings_.optionNotificationEnabled;
    soundService_.isInventoryFullSoundEnabled = settings_.invFullNotificationEnabled;
    soundService_.isBellSoundEnabled = settings_.bellNotificationEnabled;
    soundService_.isDropSoundEnabled = settings_.dropNotificationEnabled;
    soundService_.volume = settings_.soundVolume;

    settings_.Save();
    if (addSystemLog_) addSystemLog_(L"通知音設定を更新しました。");
}

void SettingsDialog::SaveTabSettings(HWND /*hwnd*/) {
    std::vector<std::wstring> selected;
    // チャットは必須
    selected.push_back(L"チャット");

    for (size_t i = 0; i < availableTabs_.size(); i++) {
        auto& name = availableTabs_[i];
        if (name == L"チャット") continue;
        auto ctrl = GetDlgItem(displayPage_, IDC_TAB_CHECK_BASE + static_cast<int>(i));
        if (ctrl && SendMessageW(ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            selected.push_back(name);
        }
    }

    currentSelectedTabs_ = selected;
    // カンマ区切りで保存
    std::wstring selStr;
    for (size_t i = 0; i < selected.size(); i++) {
        if (i > 0) selStr += L",";
        selStr += selected[i];
    }
    settings_.SaveGuiDisplayConfig(settings_.geometry, selStr);
    if (addSystemLog_) addSystemLog_(L"タブ設定を保存しました");
}

void SettingsDialog::UpdatePidDisplay(HWND /*hwnd*/) {
    auto statusCtrl = GetDlgItem(networkPage_, IDC_PID_STATUS);
    auto portsCtrl = GetDlgItem(networkPage_, IDC_PID_PORTS);
    auto ipCtrl = GetDlgItem(networkPage_, IDC_PID_IP);
    if (!statusCtrl) return;

    if (processMonitor_.IsMonitoring()) {
        auto pid = processMonitor_.GetCurrentPid();
        auto ports = processMonitor_.GetMonitoredPorts();
        wchar_t pidHex[16] = {};
        swprintf_s(pidHex, L"%08X", static_cast<unsigned int>(pid));
        std::wstring statusText = L"PID=" + std::to_wstring(pid) + L" (" + pidHex + L") 監視中";
        SetWindowTextW(statusCtrl, statusText.c_str());

        if (!ports.empty()) {
            std::wstring portStr = L"検出ポート: ";
            bool first = true;
            for (int p : ports) {
                if (!first) portStr += L", ";
                portStr += std::to_wstring(p);
                first = false;
            }
            SetWindowTextW(portsCtrl, portStr.c_str());
        } else {
            SetWindowTextW(portsCtrl, L"検出ポート: なし");
        }
        auto remoteIp = processMonitor_.GetMonitoredRemoteIp();
        if (!remoteIp.empty()) {
            SetWindowTextW(ipCtrl, (L"検出IP: " + remoteIp).c_str());
        } else {
            SetWindowTextW(ipCtrl, L"検出IP: なし");
        }
    } else {
        SetWindowTextW(statusCtrl, L"未監視");
        SetWindowTextW(portsCtrl, L"検出ポート: なし");
        SetWindowTextW(ipCtrl, L"検出IP: なし");
    }
}

void SettingsDialog::StartPidPicker(HWND hwnd) {
    pidPickerActive_ = true;
    pidPickerLastState_ = false;
    auto btn = GetDlgItem(networkPage_, IDC_PID_PICKER);
    EnableWindow(btn, FALSE);
    SetWindowTextW(btn, L"ゲーム画面をクリックしてください...");
    SetWindowTextW(GetDlgItem(networkPage_, IDC_PID_STATUS), L"クリック待機中...");
    SetTimer(hwnd, IDC_PID_TIMER, 50, nullptr);
}

void SettingsDialog::StopPidPicker(HWND hwnd) {
    pidPickerActive_ = false;
    KillTimer(hwnd, IDC_PID_TIMER);
    auto btn = GetDlgItem(networkPage_, IDC_PID_PICKER);
    if (btn) {
        EnableWindow(btn, TRUE);
        SetWindowTextW(btn, L"ゲーム画面をクリックしてウィンドウ情報を取得");
    }
}

void SettingsDialog::PollMouseClick(HWND hwnd) {
    if (!pidPickerActive_) return;
    bool currentState = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (currentState && !pidPickerLastState_) {
        POINT pt;
        if (GetCursorPos(&pt)) {
            HWND targetWnd = WindowFromPoint(pt);
            if (targetWnd) {
                DWORD pid = 0;
                GetWindowThreadProcessId(targetWnd, &pid);
                StopPidPicker(hwnd);
                OnPidPicked(hwnd, static_cast<int>(pid));
                return;
            }
        }
        StopPidPicker(hwnd);
    }
    pidPickerLastState_ = currentState;
}

void SettingsDialog::OnPidPicked(HWND hwnd, int pid) {
    try {
        processMonitor_.StartMonitoring(pid);
        if (addSystemLog_) addSystemLog_(L"PID " + std::to_wstring(pid) + L" の監視を開始しました");
    } catch (...) {
        if (addSystemLog_) addSystemLog_(L"PID " + std::to_wstring(pid) + L" の監視開始に失敗しました");
    }
    UpdatePidDisplay(hwnd);
}
