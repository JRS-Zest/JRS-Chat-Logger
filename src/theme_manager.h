#pragma once
#include "chat_type.h"
#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

// C# ThemeManager.cs の完全移植
// Win32 GUI用の色・フォント定数

namespace ThemeManager {

// --- 基調カラー ---
inline constexpr COLORREF PrimaryColor     = RGB(63, 81, 181);     // Indigo 500
inline constexpr COLORREF DarkPrimaryColor = RGB(48, 63, 159);     // Indigo 700
inline constexpr COLORREF LightPrimaryColor = RGB(197, 202, 233);  // Indigo 100
inline constexpr COLORREF AccentColor      = RGB(255, 64, 129);    // Pink A200
inline constexpr COLORREF TextIconsColor   = RGB(255, 255, 255);

// --- 色設定 ---
inline constexpr COLORREF BackgroundColor    = RGB(250, 250, 250);
inline constexpr COLORREF LogBackgroundColor = RGB(255, 245, 248);  // 極薄いピンク
inline constexpr COLORREF LogForegroundColor = RGB(40, 40, 40);     // 黒

// --- チャット種別ごとの色（薄いピンク背景に見やすい濃い色） ---
inline COLORREF GetChatTypeColor(ChatType chatType) {
    switch (chatType) {
        case ChatType::General:   return RGB(40, 40, 40);      // 黒（基本色）
        case ChatType::WhisperRx: return RGB(200, 0, 100);     // 濃いマゼンタ
        case ChatType::Party:     return RGB(0, 100, 180);     // 濃い青
        case ChatType::Guild:     return RGB(0, 140, 60);      // 濃い緑
        case ChatType::All:       return RGB(180, 120, 0);     // 濃いオレンジ
        case ChatType::ServerAll: return RGB(180, 0, 0);       // 濃い赤（鯖全）
        case ChatType::WhisperTx: return RGB(180, 0, 120);     // 濃いピンク
        default: return RGB(0, 0, 0);
    }
}

// --- フォント作成ヘルパー ---
inline HFONT CreateDefaultFont() {
    return CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
}

inline HFONT CreateLogFont() {
    return CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"MS Gothic");
}

inline HFONT CreateHeaderFont() {
    return CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                       SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
}

// --- 角丸ボタン統一デザイン ---
inline constexpr COLORREF BtnBorderColor    = RGB(180, 180, 180); // グレー枠線
inline constexpr COLORREF BtnBgNormal       = RGB(255, 255, 255); // 通常背景
inline constexpr COLORREF BtnBgPressed      = RGB(230, 230, 230); // 押下時背景
inline constexpr COLORREF BtnBgHover        = RGB(245, 245, 245); // ホバー時（簡易）
inline constexpr COLORREF BtnTextColor      = RGB(40, 40, 40);    // 文字色
inline constexpr int BtnCornerRadius        = 6;                   // 角丸半径

// WM_DRAWITEM で呼ぶ統一ボタン描画関数 (GDI+ アンチエイリアス)
inline void DrawStyledButton(DRAWITEMSTRUCT* dis) {
    if (!dis) return;
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    // 背景クリア（親の背景色で角丸外側を塗る）
    HBRUSH hParentBg = (HBRUSH)(COLOR_BTNFACE + 1);
    FillRect(hdc, &rc, hParentBg);

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    const float inset = 0.75f;
    Gdiplus::RectF rectf(
        (Gdiplus::REAL)(rc.left + inset), (Gdiplus::REAL)(rc.top + inset),
        (Gdiplus::REAL)(rc.right - rc.left - inset * 2),
        (Gdiplus::REAL)(rc.bottom - rc.top - inset * 2));
    float radius = (float)BtnCornerRadius;

    // 角丸パス作成
    Gdiplus::GraphicsPath path;
    path.AddArc(rectf.X, rectf.Y, radius * 2, radius * 2, 180, 90);
    path.AddArc(rectf.X + rectf.Width - radius * 2, rectf.Y, radius * 2, radius * 2, 270, 90);
    path.AddArc(rectf.X + rectf.Width - radius * 2, rectf.Y + rectf.Height - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(rectf.X, rectf.Y + rectf.Height - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();

    // 背景塗り
    COLORREF bg = pressed ? BtnBgPressed : BtnBgNormal;
    if (disabled) bg = RGB(240, 240, 240);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    graphics.FillPath(&brush, &path);

    // 枠線
    COLORREF border = focused ? PrimaryColor : BtnBorderColor;
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(border), GetGValue(border), GetBValue(border)), 1.0f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetAlignment(Gdiplus::PenAlignmentInset);
    graphics.DrawPath(&pen, &path);

    // テキスト描画
    wchar_t text[256] = {};
    GetWindowTextW(dis->hwndItem, text, 256);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, disabled ? RGB(160, 160, 160) : BtnTextColor);

    HFONT hFont = CreateDefaultFont();
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    RECT textRc = rc;
    if (pressed) { textRc.left += 1; textRc.top += 1; }
    DrawTextW(hdc, text, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

// 角丸ボタン作成ヘルパー (BS_OWNERDRAW + フォント設定済み)
inline HWND CreateStyledButton(const wchar_t* text, int x, int y, int w, int h,
                                HWND parent, int id) {
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        x, y, w, h, parent, (HMENU)(INT_PTR)id,
        GetModuleHandleW(nullptr), nullptr);
    HFONT hFont = CreateDefaultFont();
    SendMessageW(btn, WM_SETFONT, (WPARAM)hFont, TRUE);
    return btn;
}

// 全子ウィンドウにフォント適用
inline void ApplyFontToChildren(HWND parent, HFONT hFont) {
    EnumChildWindows(parent, [](HWND child, LPARAM lParam) -> BOOL {
        SendMessageW(child, WM_SETFONT, (WPARAM)lParam, TRUE);
        return TRUE;
    }, (LPARAM)hFont);
}

} // namespace ThemeManager

// ── ダイアログを親ウィンドウの中央に配置（画面外防止付き） ──
inline void CenterWindowOnParent(HWND hWnd, HWND hParent) {
    if (!hWnd) return;
    RECT dlgRc; GetWindowRect(hWnd, &dlgRc);
    int dlgW = dlgRc.right - dlgRc.left;
    int dlgH = dlgRc.bottom - dlgRc.top;

    RECT parentRc;
    if (hParent && IsWindow(hParent)) {
        GetWindowRect(hParent, &parentRc);
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &parentRc, 0);
    }
    int x = parentRc.left + (parentRc.right - parentRc.left - dlgW) / 2;
    int y = parentRc.top + (parentRc.bottom - parentRc.top - dlgH) / 2;

    // 画面外に出ないようクランプ
    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    if (x + dlgW > workArea.right) x = workArea.right - dlgW;
    if (y + dlgH > workArea.bottom) y = workArea.bottom - dlgH;
    if (x < workArea.left) x = workArea.left;
    if (y < workArea.top) y = workArea.top;

    SetWindowPos(hWnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}
