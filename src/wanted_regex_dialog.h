// wanted_regex_dialog.h — 通知設定ダイアログ (keywords.txt編集)
// C# WantedRegexDialog.cs の C++ 移植 (Win32)
#pragma once

#include <windows.h>

class WantedRegexService;

namespace WantedRegexDialog {
    // モーダルダイアログを表示
    bool Show(HWND hwndParent, WantedRegexService& service);
}
