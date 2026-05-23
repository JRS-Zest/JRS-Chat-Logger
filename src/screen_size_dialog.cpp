// screen_size_dialog.cpp — 画面サイズ変更ダイアログ
// C# ScreenSizeDialog.cs の C++ 移植 (Win32)
#include "screen_size_dialog.h"
#include "item_dat_loader.h"
#include "theme_manager.h"
#include "resource.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

struct SizeEntry {
    uint8_t code;
    const wchar_t* id;
    const wchar_t* size;
};

static const SizeEntry kSizes[] = {
    {0x00, L"00", L"800x600"},
    {0x01, L"01", L"1024x768"},
    {0x02, L"02", L"1280x720"},
    {0x03, L"03", L"1280x960"},
    {0x04, L"04", L"1440x1050"},
    {0x05, L"05", L"1600x800"},
    {0x06, L"06", L"1600x1200"},
    {0x07, L"07", L"1780x960"},
    {0x08, L"08", L"1920x1000"},
    {0x09, L"09", L"1920x1080"},
    {0x0A, L"0A", L"2048x1536"},
    {0x0B, L"0B", L"1680x1050"}
};
static constexpr int kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

struct ScrDlgData {
    HWND hDlg = nullptr;
    HWND hCurrentLabel = nullptr;
    HWND hNoteLabel = nullptr;
    std::wstring basePath;
};

static std::wstring GetBasePath() {
    auto reg = ItemDatLoader::GetRedStonePathFromRegistry();
    if (!reg.empty() && fs::exists(reg)) return reg;
    wchar_t pf86[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, 0, pf86) == S_OK) {
        auto fallback = fs::path(pf86) / L"GameON" / L"RED STONE";
        if (fs::exists(fallback)) return fallback.wstring();
    }
    return L"";
}

static void ReadCurrent(ScrDlgData& d) {
    d.basePath = GetBasePath();
    if (d.basePath.empty()) {
        SetWindowTextW(d.hCurrentLabel, L"現在のサイズ [??] (フォルダ未設定)");
        return;
    }
    auto cfg = (fs::path(d.basePath) / L"config.cfg").wstring();
    if (!fs::exists(cfg)) {
        SetWindowTextW(d.hCurrentLabel, L"現在のサイズ [--] (config.cfg が見つかりません)");
        return;
    }
    std::ifstream ifs(cfg, std::ios::binary);
    if (!ifs.is_open()) {
        SetWindowTextW(d.hCurrentLabel, L"現在のサイズ [--] (読み取りエラー)");
        return;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    uint8_t val = (data.size() > 150) ? data[150] : 0xFF;
    for (int i = 0; i < kSizeCount; i++) {
        if (kSizes[i].code == val) {
            wchar_t buf[128];
            swprintf_s(buf, L"現在のサイズ [(%02d) %s]", (int)val, kSizes[i].size);
            SetWindowTextW(d.hCurrentLabel, buf);
            return;
        }
    }
    wchar_t buf[64];
    swprintf_s(buf, L"現在のサイズ [%02X]", val);
    SetWindowTextW(d.hCurrentLabel, buf);
}

static void OnSizeClicked(ScrDlgData& d, uint8_t code) {
    if (d.basePath.empty()) {
        DestroyWindow(d.hDlg);
        return;
    }
    auto cfg = (fs::path(d.basePath) / L"config.cfg").wstring();
    if (!fs::exists(cfg)) {
        DestroyWindow(d.hDlg);
        return;
    }

    // config.cfg 読み込み・書き換え
    std::vector<uint8_t> data;
    {
        std::ifstream ifs(cfg, std::ios::binary);
        if (ifs.is_open()) data.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }
    if (data.size() <= 150) data.resize(151, 0);
    data[150] = code;
    {
        std::ofstream ofs(cfg, std::ios::binary);
        if (ofs.is_open()) ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    // Red Stone.exe を管理者権限で起動
    auto exe = (fs::path(d.basePath) / L"Red Stone.exe").wstring();
    if (fs::exists(exe)) {
        ShellExecuteW(nullptr, L"runas", exe.c_str(), nullptr, d.basePath.c_str(), SW_SHOWNORMAL);
    }

    DestroyWindow(d.hDlg);
}

#define IDC_SCR_BTN_BASE 4001

static const wchar_t* SCR_DLG_CLASS = L"JRSChatScreenSizeDlg";

static LRESULT CALLBACK ScrDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ScrDlgData* d = (ScrDlgData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        auto cs = (CREATESTRUCTW*)lParam;
        d = (ScrDlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        d->hDlg = hwnd;
        { HICON hIco = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON)); if (hIco) { SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIco); SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco); } }

        HFONT hFont = ThemeManager::CreateDefaultFont();
        int y = 8;

        // 現在のサイズラベル
        d->hCurrentLabel = CreateWindowExW(0, L"STATIC", L"現在のサイズ [--]",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, y, 500, 22, hwnd, nullptr, nullptr, nullptr);
        SendMessage(d->hCurrentLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 26;

        // 説明ラベル
        d->hNoteLabel = CreateWindowExW(0, L"STATIC",
            L"下記ボタンを押すと指定サイズでゲームが自動起動します",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, y, 500, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessage(d->hNoteLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 26;

        // ボタン: 4列 x 3行
        const int cols = 4;
        const int btnW = 120, btnH = 28, gapX = 6, gapY = 4;
        for (int i = 0; i < kSizeCount; i++) {
            int col = i % cols;
            int row = i / cols;
            int bx = 12 + col * (btnW + gapX);
            int by = y + row * (btnH + gapY);
            wchar_t label[64];
            swprintf_s(label, L"(%02d) %s", (int)kSizes[i].code, kSizes[i].size);
            auto hBtn = CreateWindowExW(0, L"BUTTON", label,
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                bx, by, btnW, btnH, hwnd, (HMENU)(INT_PTR)(IDC_SCR_BTN_BASE + i), nullptr, nullptr);
            SendMessage(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        // 現在のサイズ読み込み
        ReadCurrent(*d);
        return 0;
    }

    case WM_COMMAND: {
        if (!d) break;
        int id = LOWORD(wParam);
        if (id >= IDC_SCR_BTN_BASE && id < IDC_SCR_BTN_BASE + kSizeCount) {
            OnSizeClicked(*d, kSizes[id - IDC_SCR_BTN_BASE].code);
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        if (!d) break;
        auto* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlType == ODT_BUTTON) {
            ThemeManager::DrawStyledButton(dis);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ScreenSizeDialog::Show(HWND hwndParent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = ScrDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = SCR_DLG_CLASS;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    ScrDlgData data;

    const int cols = 4, btnW = 120, btnH = 28, gapX = 6, gapY = 4;
    int rows = (kSizeCount + cols - 1) / cols;
    int dlgW = 12 + cols * (btnW + gapX) + 20;
    int dlgH = 8 + 26 + 26 + rows * (btnH + gapY) + 20;

    RECT r = {0, 0, dlgW, dlgH};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX), FALSE);

    HWND hwnd = CreateWindowExW(0, SCR_DLG_CLASS, L"画面サイズ変更",
        (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        hwndParent, nullptr, GetModuleHandleW(nullptr), &data);
    if (!hwnd) return;
    CenterWindowOnParent(hwnd, hwndParent);

    // モーダルループ
    EnableWindow(hwndParent, FALSE);
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!IsWindow(hwnd)) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    EnableWindow(hwndParent, TRUE);
    SetForegroundWindow(hwndParent);
}
