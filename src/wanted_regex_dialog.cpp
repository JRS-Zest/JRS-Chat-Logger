// wanted_regex_dialog.cpp — 通知設定ダイアログ (keywords.txt編集)
// C# WantedRegexDialog.cs の C++ 移植 (Win32)
#include "wanted_regex_dialog.h"
#include "wanted_regex_service.h"
#include "theme_manager.h"
#include "resource.h"

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include <boost/regex.hpp>

struct WrdDlgData {
    WantedRegexService* service = nullptr;
    HWND hDlg = nullptr;
    HWND hInstruction = nullptr;
    HWND hEdit = nullptr;
    HWND hBtnSave = nullptr;
    HWND hBtnCheck = nullptr;
    HWND hBtnCancel = nullptr;
    bool saved = false;
};

#define IDC_WRD_EDIT    5001
#define IDC_WRD_SAVE    5002
#define IDC_WRD_CHECK   5003
#define IDC_WRD_CANCEL  5004

static const wchar_t* WRD_DLG_CLASS = L"JRSChatWantedRegexDlg";

static LRESULT CALLBACK WrdDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WrdDlgData* d = (WrdDlgData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        auto cs = (CREATESTRUCTW*)lParam;
        d = (WrdDlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        d->hDlg = hwnd;
        { HICON hIco = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON)); if (hIco) { SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIco); SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco); } }

        HFONT hFont = ThemeManager::CreateDefaultFont();
        int y = 10;

        // 説明ラベル
        const wchar_t* instructionText =
            L"▪ 通知したいアイテム/オプションの正規表現を1行ずつ記載してください。\r\n"
            L"▪ /で始まる行はコメントアウト（無効）です。\r\n"
            L"▪ 判定文字列は: [オプション名1] [オプション名2]... [アイテム名] です。\r\n"
            L"▪ 例：命中補正無視.*バトルリングUltimate\r\n"
            L"▪ 一行にアイテム名やオプション名の一部を記述するだけでも通知されます";

        d->hInstruction = CreateWindowExW(0, L"STATIC", instructionText,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, y, 460, 95, hwnd, nullptr, nullptr, nullptr);
        SendMessage(d->hInstruction, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 100;

        // テキストボックス (複数行)
        d->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
            10, y, 460, 300, hwnd, (HMENU)IDC_WRD_EDIT, nullptr, nullptr);
        HFONT hMono = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        SendMessage(d->hEdit, WM_SETFONT, (WPARAM)(hMono ? hMono : hFont), TRUE);
        y += 310;

        // 内容を読み込み
        auto content = d->service->GetContent();
        SetWindowTextW(d->hEdit, content.c_str());

        // ボタン
        // d->hBtnCheck = CreateWindowExW(0, L"BUTTON", L"正規表現チェック",
        //     WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        //     10, y, 130, 30, hwnd, (HMENU)IDC_WRD_CHECK, nullptr, nullptr);
        // SendMessage(d->hBtnCheck, WM_SETFONT, (WPARAM)hFont, TRUE);

        d->hBtnSave = CreateWindowExW(0, L"BUTTON", L"保存して閉じる",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            10, y, 130, 30, hwnd, (HMENU)IDC_WRD_SAVE, nullptr, nullptr);
        SendMessage(d->hBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);

        d->hBtnCancel = CreateWindowExW(0, L"BUTTON", L"キャンセル",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            155, y, 100, 30, hwnd, (HMENU)IDC_WRD_CANCEL, nullptr, nullptr);
        SendMessage(d->hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        return 0;
    }

    case WM_COMMAND: {
        if (!d) break;
        int id = LOWORD(wParam);
        if (id == IDC_WRD_SAVE) {
            int len = GetWindowTextLengthW(d->hEdit);
            std::wstring text(len + 1, L'\0');
            GetWindowTextW(d->hEdit, text.data(), len + 1);
            text.resize(len);
            if (d->service->SaveContent(text)) {
                d->saved = true;
                DestroyWindow(hwnd);
            } else {
                MessageBoxW(hwnd, L"保存に失敗しました", L"エラー", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        // if (id == IDC_WRD_CHECK) {
        //     CheckRegexes(*d);
        //     return 0;
        // }
        if (id == IDC_WRD_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlType == ODT_BUTTON) {
            ThemeManager::DrawStyledButton(dis);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool WantedRegexDialog::Show(HWND hwndParent, WantedRegexService& service) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = WrdDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = WRD_DLG_CLASS;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    WrdDlgData data;
    data.service = &service;

    RECT r = {0, 0, 490, 480};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX), FALSE);

    HWND hwnd = CreateWindowExW(0, WRD_DLG_CLASS, L"通知設定",
        (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        hwndParent, nullptr, GetModuleHandleW(nullptr), &data);
    if (!hwnd) return false;
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
    return data.saved;
}
