// dc_setting_dialog.h — DCSettingDialog: ドロップクリーン設定ダイアログ
// C# DCSettingDialog.cs の C++ 移植 (Win32)
#pragma once

#include <windows.h>
#include <string>

class DCManager;
class AppSettings;
class ItemDatLoader;

namespace DCSettingDialog {
    // モーダルダイアログを表示
    void Show(HWND hwndParent, DCManager& manager, AppSettings& settings, ItemDatLoader& itemDatLoader);
}
