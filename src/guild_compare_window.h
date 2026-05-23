// guild_compare_window.h — ギルド員比較ウィンドウ
#pragma once

#include <windows.h>
#include <string>

// 比較ウィンドウを表示する。既に開いていれば前面に出す。
// guildFolder: ログ\ギルド員 フォルダのフルパス
void ShowGuildCompareWindow(HINSTANCE hInst, HWND hParent,
                            const std::wstring& guildFolder, HFONT hFont);
