// main.cpp — JRS Chat Logger (C++) 
// C# MainForm.cs の完全Win32移植
// 全サービスモジュールを統合するエントリポイント

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <richedit.h>
#include <gdiplus.h>

#include "resource.h"

#include <string>
#include <vector>
#include <set>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <optional>

// プロジェクトヘッダ（全モジュール）
#include "app_settings.h"
#include "chat_type.h"
#include "network_helper.h"
#include "packet_capture_service.h"
#include "packet_crypt.h"
#include "item_dat_loader.h"
#include "chat_packet_processor.h"
#include "wanted_regex_service.h"
#include "item_packet_processor.h"
#include "sound_notification_service.h"
#include "bell_processor.h"
#include "chat_logger_service.h"
#include "item_logger_service.h"
#include "process_monitor_service.h"
#include "theme_manager.h"
#include "settings_dialog.h"
#include "rate_roten_processor.h"
#include "roten_processor.h"
#include "gold_processor.h"
#include "dc_manager.h"
#include "dc_setting_dialog.h"
#include "screen_size_dialog.h"
#include "wanted_regex_dialog.h"
#include "drop_packet_processor.h"
#include "special_drop_processor.h"
#include "equipment_packet_processor.h"
#include "guild_member_processor.h"
#include "guild_compare_window.h"



// ───────────────────────── 定数 ─────────────────────────
static const wchar_t CLASS_NAME[] = L"JRSChatMainWindow";
static const int TOOLBAR_HEIGHT = 28;
static const int TOOLBAR_BTN_HEIGHT = 22;
static const int STATUS_HEIGHT = 22;
static const int MAX_CHAT_LINES = 5000;
static const int MAX_RECENT_ITEMS = 10000;
// コントロール ID
#define IDC_BTN_STOP     1001
#define IDC_BTN_START    1002
#define IDC_BTN_DC       1004
#define IDC_BTN_SCREEN   1005
#define IDC_BTN_SETTINGS 1006
#define IDC_BTN_MAXV     1007
#define IDC_UNSEEN       2001
#define IDC_BELL         2002
#define IDC_NOTEBOOK     2010
#define IDC_CHAT_EDIT    2020
#define IDC_ITEM_LIST    2030
#define IDC_LATEST_LABEL 2040
#define IDC_EVENT_LOG    2050
#define IDC_ITEM_NOTIFY_BTN  2060
#define IDC_ITEM_SHOW_ALL    2061
#define IDC_EVENT_LOG_TITLE  2062
#define IDC_RATE_ROTEN_LIST  2070
#define IDC_RATE_ROTEN_VIEWER_BTN 2071
#define IDC_ROTEN_EDIT       2080
#define IDC_ROTEN_SNAP_BTN   2081
#define IDC_ROTEN_VIEWER_BTN 2082
// 収入タブ
#define IDC_GOLD_BTN_START   2090
#define IDC_GOLD_BTN_STOP    2091
#define IDC_GOLD_BTN_RESET   2092
#define IDC_GOLD_BTN_SAVE    2093
// ドロップタブ
#define IDC_DROP_LIST        2100
#define IDC_DROP_SHOW_ALL    2101
#define IDC_DROP_DC_COMBO    2102
// 装備タブ
#define IDC_EQUIP_LIST       2120
#define IDC_EQUIP_COPY_CSV   2121
#define IDC_EQUIP_COPY_IMG   2122
// チャットフィルタボタン
#define IDC_CHAT_FILTER_ALL     2110
#define IDC_CHAT_FILTER_GENERAL 2111
#define IDC_CHAT_FILTER_PARTY   2112
#define IDC_CHAT_FILTER_GUILD   2113
#define IDC_CHAT_FILTER_WHISPER 2114
#define IDC_CHAT_FILTER_COUNT   5
#define IDC_CHAT_AUTOSCROLL     2115
// タイマー ID
#define TIMER_BLINK          1
#define TIMER_CLEANUP        2
#define TIMER_EVENTLOG_BLINK 3
#define TIMER_EVENTLOG_CLEAR 4
#define TIMER_CELL_BLINK     5
#define TIMER_DROP_BLINK     6
// WM_APP メッセージ
#define WM_CHAT_MSG      (WM_APP + 1)   // lParam = ChatMessage*
#define WM_ITEM_PICKUP   (WM_APP + 2)   // lParam = ItemPickupInfo*
#define WM_INV_FULL      (WM_APP + 3)   // wParam = 0
#define WM_BELL_LABEL    (WM_APP + 4)   // lParam = wchar_t* (delete[])
#define WM_BELL_CHAT     (WM_APP + 5)   // lParam = pair<wstring,wstring>*
#define WM_CAPTURE_STATE (WM_APP + 6)   // wParam = running(1/0)
#define WM_SYSTEM_LOG    (WM_APP + 7)   // lParam = wchar_t* (delete[])
#define WM_PORTS_UPDATED (WM_APP + 8)   // lParam = set<int>*
#define WM_AUTO_START    (WM_APP + 9)
#define WM_STARTUP_DONE  (WM_APP + 10)
#define WM_RATE_ROTEN    (WM_APP + 12)  // lParam = RateRotenResult*
#define WM_ROTEN_LOG     (WM_APP + 13)  // lParam = RotenLogEntry*
#define WM_GOLD_UPDATE   (WM_APP + 14)  // wParam = 0 (UI更新シグナル)
#define WM_ROTEN_VIEWER_REFRESH (WM_APP + 15)  // ビューア画像リフレッシュ
#define WM_DROP_ITEM    (WM_APP + 16)  // lParam = DropItemInfo*
#define WM_EQUIP_DATA   (WM_APP + 17)  // lParam = EquipmentData*
#define WM_GUILD_MEMBER (WM_APP + 18)  // lParam = GuildMemberResult*

// ───────────────────────── グローバルHWND ─────────────────────────
static HWND g_hwndMain   = nullptr;
static HWND g_hNotebook  = nullptr;
static HWND g_hChatEdits[IDC_CHAT_FILTER_COUNT] = {};  // 0=全表示,1=一般/全体,2=パーティ,3=ギルド,4=ささやき
static HWND g_hItemList  = nullptr;
static HWND g_hBtnStop   = nullptr;
static HWND g_hBtnStart  = nullptr;
static HWND g_hBtnDC     = nullptr;
static HWND g_hBtnScreen = nullptr;
static HWND g_hBtnSettings = nullptr;
static HWND g_hBtnMaxV   = nullptr;
static HWND g_hUnseenLabel = nullptr;
static HWND g_hBellLabel   = nullptr;
static HWND g_hLatestLabel = nullptr;   // ステータスバー（最新チャット表示）
static HWND g_hItemEventLog = nullptr;  // アイテムタブ EventLog ラベル
static HWND g_hItemEventTitle = nullptr;
static HWND g_hItemNotifyBtn = nullptr;
static HWND g_hItemShowAll   = nullptr;
static HWND g_hToolTip = nullptr;
static HWND g_hRateRotenList = nullptr;     // 露店価格 ListView
static HWND g_hRateRotenViewerBtn = nullptr; // 露店価格Viewer ボタン
static HWND g_hRotenEdit = nullptr;          // 露店売買 RichEdit
static HWND g_hRotenSnapBtn = nullptr;       // Snap ボタン
static HWND g_hRotenViewerBtn = nullptr;     // 露店開始画像ボタン
// 収入タブ
static HWND g_hGoldBtnStart   = nullptr;
static HWND g_hGoldBtnStop    = nullptr;
static HWND g_hGoldBtnReset   = nullptr;
static HWND g_hGoldBtnSave    = nullptr;
static HWND g_hGoldMobGroup   = nullptr;
static HWND g_hGoldMobCountVal = nullptr;
static HWND g_hGoldIncomeGroup = nullptr;
static HWND g_hGoldMobVal     = nullptr;
static HWND g_hGoldPickupVal  = nullptr;
static HWND g_hGoldMerchantVal = nullptr;
static HWND g_hGoldTotalVal   = nullptr;
static HWND g_hGoldDecompGroup = nullptr;
static HWND g_hGoldDecompVal  = nullptr;
static HWND g_hGoldCrystalVal = nullptr;
static HWND g_hGoldUmuVal     = nullptr;
// 収入タブ内ラベル (MobText, labels inside income/decomp groups)
static HWND g_hGoldLblMobText  = nullptr;
static HWND g_hGoldLblMob      = nullptr;
static HWND g_hGoldLblPickup   = nullptr;
static HWND g_hGoldLblMerchant = nullptr;
static HWND g_hGoldLblTotal    = nullptr;
static HWND g_hGoldLblDecomp   = nullptr;
static HWND g_hGoldLblCrystal  = nullptr;
static HWND g_hGoldLblUmu      = nullptr;
// ドロップタブ
static HWND g_hDropList    = nullptr;   // ドロップ ListView
static HWND g_hDropShowAll = nullptr;   // 全表示チェックボックス
static HWND g_hDropDcCombo = nullptr;   // DCフォルダ選択 ComboBox
static HWND g_hDropLblAll  = nullptr;   // "全表示" ラベル
static HWND g_hDropLblDc   = nullptr;   // "通知用DC" ラベル
// 装備タブ
static HWND g_hEquipList   = nullptr;
static HWND g_hEquipCopyBtn = nullptr;
static HWND g_hEquipImgBtn  = nullptr;
static HWND g_hEquipLabel  = nullptr;
// G員タブ
static HWND g_hGuildList   = nullptr;

// ───────────────────────── グローバル状態 ─────────────────────────
static ULONG_PTR g_gdiplusToken = 0;
static HFONT g_uiFont     = nullptr;
static HFONT g_uiFontBold = nullptr;
static HFONT g_logFont    = nullptr;
static Gdiplus::Bitmap* g_settingsBitmap = nullptr;

static std::atomic<bool> g_running{false};
static std::atomic<bool> g_bellActive{false};
static bool g_chatTabHasUnread = false;
static bool g_isVerticalMaximized = false;
static int  g_savedHeight = 0;
static int  g_savedY = 0;
static int  g_chatLineCounts[IDC_CHAT_FILTER_COUNT] = {};
static bool g_startupLinksShown = false;
static bool g_showAllItems = false;
static bool g_showAllDrops = false;

// ───────────────────────── チャットフィルタ ─────────────────────────
static const int CHAT_FILTER_ROW_HEIGHT = 20;
static const int MAX_CHAT_BUFFER = 1000;

// バッファエントリ: チャットメッセージ or システムログ
struct ChatBufferEntry {
    bool isSystem = false;           // true=システムログ
    ChatMessage chatMsg{};           // isSystem=false時有効
    std::wstring systemText;         // isSystem=true時のフォーマット済みテキスト
};
static std::deque<ChatBufferEntry> g_chatBuffer;

// フィルタボタン HWND / 状態
static HWND g_hChatFilterBtns[IDC_CHAT_FILTER_COUNT] = {};
static int g_chatFilterActive = 0;  // 0=全表示, 1=一般/全体, 2=パーティ, 3=ギルド, 4=ささやき
static const wchar_t* g_chatFilterLabels[IDC_CHAT_FILTER_COUNT] = {
    L"全表示", L"一般/全体", L"パーティ", L"ギルド", L"ささやき"
};
static HFONT g_chatFilterFont = nullptr;
static HWND g_hChatAutoScroll = nullptr;
static bool g_chatAutoScroll = true;

// EventLog 点滅
static int  g_eventLogBlinkCount = 0;
static bool g_eventLogBlinkOn = false;
static std::wstring g_eventLogLastMessage;

// タブ管理
static std::vector<std::wstring> g_masterTabOrder = {L"チャット", L"露店価格", L"露店売買", L"アイテム", L"収入", L"ドロップ", L"装備", L"G員"};
static std::vector<std::wstring> g_selectedTabs;
// タブページ → タブ名のマップ（インデックス）
// _tabPages に相当（タブ名→HWND child window）
struct TabPageInfo {
    std::wstring name;
    HWND hPage = nullptr;   // タブページ内コンテナ（STATIC window）
};
static std::vector<TabPageInfo> g_visibleTabs;

// アイテムリスト
struct ItemEntry {
    std::wstring time;
    std::wstring name;
    std::wstring op1, op2, op3;
    bool isNotificationTarget = false;
    // セル別マッチ情報 (col 1=item, 2=op1, 3=op2, 4=op3)
    bool cellMatch[5] = {};  // col0(時刻)は常はfalse
};
static std::vector<ItemEntry> g_items;
static std::mutex g_items_mutex;

// セル点滅管理
struct BlinkCell {
    int row;         // g_items インデックス
    int col;         // ListView カラムインデックス
    int remaining;   // 残りティック数
    bool isOn;       // 点灯中か
};
static std::vector<BlinkCell> g_blinkCells;

// ドロップアイテム一覧
struct DropEntry {
    std::chrono::system_clock::time_point timestamp{};
    std::wstring itemName;
    std::wstring coordText;
    bool isNotificationTarget = false;
    bool notifyBlink = false;      // 点滅表示中
    bool notifyHighlight = false;  // 点滅終了後の恒久ハイライト
};
static std::vector<DropEntry> g_drops;
static std::mutex g_drops_mutex;

// ドロップ行点滅管理
struct DropBlinkRow {
    int row;
    int remaining;
    bool isOn;
};
static std::vector<DropBlinkRow> g_dropBlinkRows;

// 装備スロット一覧 (常に18要素、受信時に上書き)
struct EquipEntry {
    std::wstring slotName;
    std::wstring itemName;
    std::wstring opName[3];
    std::wstring opVal[3];
};
static std::vector<EquipEntry> g_equips;

// G員一覧 (受信ごとに上書き)
struct GuildEntry {
    std::wstring name;
    std::wstring level;
    std::wstring job;
    std::wstring rank;
};
static std::vector<GuildEntry> g_guildMembers;
static std::wstring g_guildName;
static HWND g_hGuildCompareBtn = nullptr;
static HWND g_hGuildIntervalLabel = nullptr;
static HWND g_hGuildIntervalCombo = nullptr;
static HWND g_hGuildManualSaveBtn = nullptr;
static HWND g_hGuildHelpBtn = nullptr;
static Gdiplus::Bitmap* g_helpBitmap = nullptr;
static std::optional<GuildMemberResult> g_lastGuildResult;

// ───────────────────────── サービスインスタンス ─────────────────────────
static std::unique_ptr<AppSettings>              g_settings;
static std::unique_ptr<ItemDatLoader>            g_itemDatLoader;
static std::unique_ptr<WantedRegexService>       g_wantedRegex;
static std::unique_ptr<ChatPacketProcessor>      g_chatProcessor;
static std::unique_ptr<ItemPacketProcessor>      g_itemProcessor;
static std::unique_ptr<SoundNotificationService> g_soundService;
static std::unique_ptr<BellProcessor>            g_bellProcessor;
static std::unique_ptr<ChatLoggerService>        g_chatLogger;
static std::unique_ptr<ItemLoggerService>        g_itemLogger;
static std::unique_ptr<ProcessMonitorService>    g_processMonitor;
static std::unique_ptr<PacketCaptureService>     g_captureService;
static std::unique_ptr<RateRotenProcessor>       g_rateRotenProcessor;
static std::unique_ptr<RotenProcessor>           g_rotenProcessor;
static std::unique_ptr<GoldProcessor>            g_goldProcessor;
static std::unique_ptr<DCManager>                g_dcManager;
static std::unique_ptr<DropPacketProcessor>      g_dropProcessor;
static std::unique_ptr<SpecialDropProcessor>    g_specialDropProcessor;
static std::unique_ptr<EquipmentPacketProcessor> g_equipProcessor;
static std::unique_ptr<GuildMemberProcessor>        g_guildMemberProcessor;

// ───────────────────────── ユーティリティ ─────────────────────────
static std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    auto pos = p.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? p.substr(0, pos) : L".";
}

static std::wstring FormatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    struct tm tmv{};
    localtime_s(&tmv, &t);
    wchar_t buf[64];
    swprintf_s(buf, L"%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

// PostMessage でヒープ文字列を送る
static void PostSystemLog(HWND hwnd, const std::wstring& msg) {
    wchar_t* buf = new wchar_t[msg.size() + 1];
    wcscpy_s(buf, msg.size() + 1, msg.c_str());
    PostMessage(hwnd, WM_SYSTEM_LOG, 0, (LPARAM)buf);
}

// ───────────────────────── アイテム管理 ─────────────────────────
static void AddItemEntry(const ItemEntry& ie) {
    std::lock_guard<std::mutex> lk(g_items_mutex);
    g_items.push_back(ie);
    if ((int)g_items.size() > MAX_RECENT_ITEMS) {
        g_items.erase(g_items.begin());
    }
    if (g_hItemList) {
        ListView_SetItemCount(g_hItemList, (int)g_items.size());
        ListView_EnsureVisible(g_hItemList, (int)g_items.size() - 1, FALSE);
    }
}

static void RefreshItemListView() {
    if (!g_hItemList) return;
    {
        std::lock_guard<std::mutex> lk(g_items_mutex);
        // g_showAllItems でフィルタ（ここでは全件仮想表示のまま）
        ListView_SetItemCount(g_hItemList, (int)g_items.size());
    }
    InvalidateRect(g_hItemList, nullptr, TRUE);
}

// ───────────────────────── ドロップ管理 ─────────────────────────
// フィルタ済みのインデックスリストを返す
static std::vector<int> GetFilteredDropIndices() {
    std::vector<int> indices;
    for (int i = 0; i < (int)g_drops.size(); i++) {
        if (g_showAllDrops || g_drops[i].isNotificationTarget)
            indices.push_back(i);
    }
    return indices;
}

static void RefreshDropListView() {
    if (!g_hDropList) return;
    int count = 0;
    {
        std::lock_guard<std::mutex> lk(g_drops_mutex);
        auto filtered = GetFilteredDropIndices();
        count = (int)filtered.size();
    }
    ListView_SetItemCount(g_hDropList, count);
    if (count > 0) ListView_EnsureVisible(g_hDropList, count - 1, FALSE);
    InvalidateRect(g_hDropList, nullptr, TRUE);
}

static void AdjustListViewColumns(HWND hList, const int minWidths[], const int desired[], int nCols);
static void AdjustDropColumns() {
    static const int minW[] = { 90, 120, 80 };
    static const int desW[] = { 90, 260, 120 };
    AdjustListViewColumns(g_hDropList, minW, desW, 3);
}

static void AdjustEquipColumns() {
    // Slot, Name, OP1, OP2, OP3 = 5列
    static const int minW[] = { 50, 100, 80, 80, 80 };
    static const int desW[] = { 55, 160, 120, 120, 120 };
    AdjustListViewColumns(g_hEquipList, minW, desW, 5);
}

static void RefreshGuildListView() {
    if (!g_hGuildList) return;
    ListView_SetItemCount(g_hGuildList, (int)g_guildMembers.size());
    InvalidateRect(g_hGuildList, nullptr, TRUE);
}

static void AdjustGuildColumns() {
    // Name, Lv, Job, Rank = 4列
    static const int minW[] = { 100, 40, 60, 60 };
    static const int desW[] = { 180, 50, 80, 80 };
    AdjustListViewColumns(g_hGuildList, minW, desW, 4);
}

// ───────────────────────── 装備テーブル画像生成 ─────────────────────────
static Gdiplus::Bitmap* RenderEquipTableImage() {
    if (g_equips.empty()) return nullptr;

    // --- 配色 (旧: ゲーム風ゴールド) ---
    // const Gdiplus::Color colBg(255, 26, 26, 46);        // #1A1A2E
    // const Gdiplus::Color colHeaderBg(255, 22, 33, 62);   // #16213E
    // const Gdiplus::Color colRowAlt(255, 22, 33, 62);     // ゼブラ暗
    // const Gdiplus::Color colGold(255, 240, 192, 64);     // #F0C040  ヘッダー文字
    // const Gdiplus::Color colText(255, 232, 213, 163);    // #E8D5A3  データ文字
    // const Gdiplus::Color colEmpty(255, 74, 74, 106);     // #4A4A6A  空スロット
    // const Gdiplus::Color colLine(255, 40, 40, 70);       // 罫線

    // --- 配色 (ディープ・ミニマル・ダーク ハイブリッド) ---
    const Gdiplus::Color colBg(255, 42, 46, 53);          // #2A2E35 背景フラット
    const Gdiplus::Color colHdrBg(255, 30, 58, 74);       // #1E3A4A ヘッダー(暗ティール)
    const Gdiplus::Color colRowOdd(255, 52, 58, 64);      // #343A40 奇数行
    const Gdiplus::Color colRowEven(255, 46, 51, 56);     // #2E3338 偶数行
    const Gdiplus::Color colAccent(255, 26, 188, 156);    // #1ABC9C アクセント(ティール)
    // 文字色
    const Gdiplus::Color colText(255, 248, 249, 250);     // #F8F9FA オフホワイト
    const Gdiplus::Color colEmpty(255, 108, 117, 125);    // #6C757D 空スロット

    // --- フォント (UnitPixel: DPI非依存で安定描画) ---
    Gdiplus::FontFamily family(L"Meiryo");
    Gdiplus::Font fontHeader(&family, 16.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font fontData(&family, 16.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    // --- 列ヘッダー ---
    const wchar_t* headers[] = { L"Slot", L"Name", L"OP1", L"OP2", L"OP3" };
    const int nCols = 5;
    const int padX = 10;   // セル内左右パディング
    const int padY = 4;    // セル内上下パディング

    // --- 文字列幅計測 (一時Bitmap使用) ---
    Gdiplus::Bitmap tmpBmp(1, 1, PixelFormat32bppARGB);
    Gdiplus::Graphics tmpG(&tmpBmp);
    tmpG.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    // 各列の最大幅を計測 (MeasureStringはCJKで過小評価するためceil+余白)
    int colW[nCols] = {};
    Gdiplus::RectF bound;
    Gdiplus::StringFormat sfTypo(Gdiplus::StringFormat::GenericTypographic());
    sfTypo.SetFormatFlags(sfTypo.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap);
    for (int c = 0; c < nCols; c++) {
        tmpG.MeasureString(headers[c], -1, &fontHeader, Gdiplus::PointF(0, 0), &sfTypo, &bound);
        colW[c] = (int)ceilf(bound.Width) + padX * 2 + 4;
    }
    int rowH = 0;
    for (auto& e : g_equips) {
        const std::wstring* fields[] = { &e.slotName, &e.itemName, &e.opName[0], &e.opName[1], &e.opName[2] };
        for (int c = 0; c < nCols; c++) {
            if (!fields[c]->empty()) {
                tmpG.MeasureString(fields[c]->c_str(), -1, &fontData, Gdiplus::PointF(0, 0), &sfTypo, &bound);
                int w = (int)ceilf(bound.Width) + padX * 2 + 4;
                if (w > colW[c]) colW[c] = w;
                int h = (int)ceilf(bound.Height) + padY * 2;
                if (h > rowH) rowH = h;
            }
        }
    }
    // ヘッダー高さ
    tmpG.MeasureString(L"Ag", -1, &fontHeader, Gdiplus::PointF(0, 0), &bound);
    int headerH = (int)bound.Height + padY * 2;
    if (rowH < headerH) rowH = headerH;

    // 画像サイズ計算
    int totalW = 0;
    for (int c = 0; c < nCols; c++) totalW += colW[c];
    int totalH = headerH + rowH * (int)g_equips.size();

    // --- 描画 ---
    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(totalW, totalH, PixelFormat32bppARGB);
    Gdiplus::Graphics g(bmp);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // 背景フラット
    Gdiplus::SolidBrush brBg(colBg);
    g.FillRectangle(&brBg, 0, 0, totalW, totalH);

    // ヘッダー背景フラット
    Gdiplus::SolidBrush brHdrBg(colHdrBg);
    g.FillRectangle(&brHdrBg, 0, 0, totalW, headerH);

    // ヘッダー文字
    Gdiplus::SolidBrush brHdrText(colText);
    int x = 0;
    for (int c = 0; c < nCols; c++) {
        Gdiplus::PointF pt((Gdiplus::REAL)(x + padX), (Gdiplus::REAL)padY);
        g.DrawString(headers[c], -1, &fontHeader, pt, &brHdrText);
        x += colW[c];
    }

    // ヘッダー下線 (アクセント色 1px)
    Gdiplus::Pen penAccent(colAccent, 1.0f);
    g.DrawLine(&penAccent, 0, headerH - 1, totalW, headerH - 1);

    // データ行
    Gdiplus::SolidBrush brText(colText);
    Gdiplus::SolidBrush brEmpty(colEmpty);
    Gdiplus::SolidBrush brAccentText(colAccent);
    Gdiplus::SolidBrush brRowOdd(colRowOdd);
    Gdiplus::SolidBrush brRowEven(colRowEven);

    for (int r = 0; r < (int)g_equips.size(); r++) {
        int y = headerH + r * rowH;

        // ゼブラストライプ (奇数/偶数ソリッド)
        g.FillRectangle((r % 2 == 0) ? &brRowOdd : &brRowEven, 0, y, totalW, rowH);

        auto& e = g_equips[r];
        const std::wstring* fields[] = { &e.slotName, &e.itemName, &e.opName[0], &e.opName[1], &e.opName[2] };
        bool isEmpty = e.itemName.empty();

        x = 0;
        for (int c = 0; c < nCols; c++) {
            if (!fields[c]->empty()) {
                Gdiplus::PointF pt((Gdiplus::REAL)(x + padX), (Gdiplus::REAL)(y + padY));
                // 空スロット→灰色、OP列(c>=2)→アクセント色、その他→オフホワイト
                Gdiplus::SolidBrush& br = (isEmpty && c == 0) ? brEmpty
                    : (c >= 2 && !isEmpty) ? brAccentText : brText;
                g.DrawString(fields[c]->c_str(), -1, &fontData, pt, &br);
            }
            x += colW[c];
        }
    }

    // 列区切り線・外枠・行罫線は廃止（フラットデザイン）

    return bmp;  // 呼び出し元が delete する
}

// ───────────────────────── PNG保存エンコーダCLSID取得 ─────────────────────────
static int GetEncoderClsid(const wchar_t* mimeType, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    auto* info = (Gdiplus::ImageCodecInfo*)malloc(size);
    if (!info) return -1;
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(info[i].MimeType, mimeType) == 0) {
            *pClsid = info[i].Clsid;
            free(info);
            return (int)i;
        }
    }
    free(info);
    return -1;
}

// ───────────────────────── ファイルをクリップボードにコピー (CF_HDROP) ─────────────────────────
static bool CopyFileToClipboard(HWND hwnd, const std::wstring& filePath) {
    size_t pathBytes = (filePath.size() + 1) * sizeof(wchar_t);
    size_t totalSize = sizeof(DROPFILES) + pathBytes + sizeof(wchar_t); // 終端ダブルNULL
    HGLOBAL hMem = GlobalAlloc(GHND, totalSize);
    if (!hMem) return false;
    auto* df = (DROPFILES*)GlobalLock(hMem);
    if (!df) { GlobalFree(hMem); return false; }
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    memcpy((BYTE*)df + sizeof(DROPFILES), filePath.c_str(), pathBytes);
    // ダブルNULL終端 (GlobalAlloc GHNDでゼロ初期化済み)
    GlobalUnlock(hMem);
    if (!OpenClipboard(hwnd)) { GlobalFree(hMem); return false; }
    EmptyClipboard();
    SetClipboardData(CF_HDROP, hMem);
    CloseClipboard();
    return true;
}

// ───────────────────────── 装備画像保存後ダイアログ ─────────────────────────
static const wchar_t EQUIP_SAVE_DLG_CLASS[] = L"EquipSaveDlgClass";
static int g_equipSaveDlgResult = 0;     // 0=Cancel, 1=Copy, 2=OpenFolder
static std::wstring g_equipSaveDlgPath;  // PNG ファイルパス
static std::wstring g_equipSaveDlgDir;   // フォルダパス
static HWND g_equipSaveDlgHwnd = nullptr;

static LRESULT CALLBACK EquipSaveDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        // ラベル
        HWND hLabel = CreateWindowExW(0, L"STATIC", L"装備画像を保存しました",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            10, 15, 300, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        // ボタン: 画像をコピー
        HWND hCopy = CreateWindowExW(0, L"BUTTON", L"画像をコピー",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            15, 50, 90, 28, hwnd, (HMENU)1, nullptr, nullptr);
        SendMessage(hCopy, WM_SETFONT, (WPARAM)hFont, TRUE);
        // ボタン: フォルダを開く
        HWND hOpen = CreateWindowExW(0, L"BUTTON", L"フォルダを開く",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            115, 50, 100, 28, hwnd, (HMENU)2, nullptr, nullptr);
        SendMessage(hOpen, WM_SETFONT, (WPARAM)hFont, TRUE);
        // ボタン: キャンセル
        HWND hCancel = CreateWindowExW(0, L"BUTTON", L"キャンセル",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            225, 50, 80, 28, hwnd, (HMENU)3, nullptr, nullptr);
        SendMessage(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1: // 画像をコピー
            g_equipSaveDlgResult = 1;
            DestroyWindow(hwnd);
            return 0;
        case 2: // フォルダを開く
            g_equipSaveDlgResult = 2;
            DestroyWindow(hwnd);
            return 0;
        case 3: // キャンセル
            g_equipSaveDlgResult = 0;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        g_equipSaveDlgResult = 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static int ShowEquipSaveDialog(HWND parent, const std::wstring& filePath, const std::wstring& dirPath) {
    g_equipSaveDlgResult = 0;
    g_equipSaveDlgPath = filePath;
    g_equipSaveDlgDir = dirPath;

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = EquipSaveDlgProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = EQUIP_SAVE_DLG_CLASS;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        classRegistered = true;
    }

    RECT pr{};
    GetWindowRect(parent, &pr);
    int cx = (pr.left + pr.right) / 2 - 160;
    int cy = (pr.top + pr.bottom) / 2 - 50;

    g_equipSaveDlgHwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        EQUIP_SAVE_DLG_CLASS, L"装備画像保存",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        cx, cy, 330, 120,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!g_equipSaveDlgHwnd) return 0;

    EnableWindow(parent, FALSE);
    ShowWindow(g_equipSaveDlgHwnd, SW_SHOW);
    UpdateWindow(g_equipSaveDlgHwnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    g_equipSaveDlgHwnd = nullptr;
    return g_equipSaveDlgResult;
}

static void RefreshEquipListView() {
    if (!g_hEquipList) return;
    ListView_SetItemCount(g_hEquipList, (int)g_equips.size());
    InvalidateRect(g_hEquipList, nullptr, TRUE);
}

// ───────────────────────── RichEdit ヘルパー ─────────────────────────
// HWND → g_hChatEdits のインデックスを返す
static int ChatEditIndex(HWND hRich) {
    for (int i = 0; i < IDC_CHAT_FILTER_COUNT; i++)
        if (g_hChatEdits[i] == hRich) return i;
    return 0;
}
// ChatType → フィルタインデックス (1-4)。該当なしは -1
static int ChatTypeFilterIndex(ChatType ct) {
    switch (ct) {
        case ChatType::General:
        case ChatType::All:
        case ChatType::ServerAll:   return 1;
        case ChatType::Party:       return 2;
        case ChatType::Guild:       return 3;
        case ChatType::WhisperRx:
        case ChatType::WhisperTx:   return 4;
        default:                    return -1;
    }
}

// ───────────────────────── RichEdit チャット追加 ─────────────────────────
static void AppendChatText(HWND hRich, const std::wstring& text, COLORREF color) {
    if (!hRich) return;
    int idx = ChatEditIndex(hRich);

    // AutoScroll OFF: スクロール位置・選択範囲保存
    POINT scrollPos{};
    CHARRANGE selRange{};
    if (!g_chatAutoScroll) {
        SendMessage(hRich, EM_GETSCROLLPOS, 0, (LPARAM)&scrollPos);
        SendMessage(hRich, EM_EXGETSEL, 0, (LPARAM)&selRange);
    }

    int len = GetWindowTextLengthW(hRich);
    SendMessage(hRich, EM_SETSEL, (WPARAM)len, (LPARAM)len);

    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BACKCOLOR;
    cf.crTextColor = color;
    cf.crBackColor = ThemeManager::LogBackgroundColor;
    SendMessage(hRich, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    SendMessage(hRich, EM_REPLACESEL, 0, (LPARAM)text.c_str());

    if (g_chatAutoScroll) {
        SendMessage(hRich, WM_VSCROLL, SB_BOTTOM, 0);
    } else {
        SendMessage(hRich, EM_EXSETSEL, 0, (LPARAM)&selRange);
        SendMessage(hRich, EM_SETSCROLLPOS, 0, (LPARAM)&scrollPos);
    }

    g_chatLineCounts[idx]++;
    // 行数制限
    if (g_chatLineCounts[idx] > MAX_CHAT_LINES) {
        SendMessage(hRich, EM_SETSEL, 0, 0);
        int firstNewLine = (int)SendMessage(hRich, EM_LINEINDEX, 1, 0);
        if (firstNewLine > 0) {
            SendMessage(hRich, EM_SETSEL, 0, firstNewLine);
            SendMessage(hRich, EM_REPLACESEL, 0, (LPARAM)L"");
            g_chatLineCounts[idx]--;
        }
    }
}

static void AppendWhisperText(HWND hRich, const std::wstring& text, COLORREF color) {
    if (!hRich) return;
    int idx = ChatEditIndex(hRich);

    // AutoScroll OFF: スクロール位置・選択範囲保存
    POINT scrollPos{};
    CHARRANGE selRange{};
    if (!g_chatAutoScroll) {
        SendMessage(hRich, EM_GETSCROLLPOS, 0, (LPARAM)&scrollPos);
        SendMessage(hRich, EM_EXGETSEL, 0, (LPARAM)&selRange);
    }

    int len = GetWindowTextLengthW(hRich);
    SendMessage(hRich, EM_SETSEL, (WPARAM)len, (LPARAM)len);

    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BACKCOLOR;
    cf.crTextColor = color;
    cf.crBackColor = RGB(255, 220, 230); // ささやき受信は薄いピンク背景
    SendMessage(hRich, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    SendMessage(hRich, EM_REPLACESEL, 0, (LPARAM)text.c_str());

    if (g_chatAutoScroll) {
        SendMessage(hRich, WM_VSCROLL, SB_BOTTOM, 0);
    } else {
        SendMessage(hRich, EM_EXSETSEL, 0, (LPARAM)&selRange);
        SendMessage(hRich, EM_SETSCROLLPOS, 0, (LPARAM)&scrollPos);
    }

    g_chatLineCounts[idx]++;
}

// フィルタボタン切替処理（単一選択式）― ShowWindow のみで瞬時切替
static void OnChatFilterToggle(int btnIndex) {
    if (btnIndex == g_chatFilterActive) return;
    // 旧 RichEdit を非表示
    if (g_hChatEdits[g_chatFilterActive])
        ShowWindow(g_hChatEdits[g_chatFilterActive], SW_HIDE);
    g_chatFilterActive = btnIndex;
    // 新 RichEdit を表示
    if (g_hChatEdits[g_chatFilterActive]) {
        ShowWindow(g_hChatEdits[g_chatFilterActive], SW_SHOW);
        // 表示後にスクロール位置を底に再補正（非表示中のレイアウト遅延対策）
        if (g_chatAutoScroll)
            SendMessage(g_hChatEdits[g_chatFilterActive], WM_VSCROLL, SB_BOTTOM, 0);
    }
    // 全ボタン再描画
    for (int i = 0; i < IDC_CHAT_FILTER_COUNT; i++) {
        if (g_hChatFilterBtns[i]) InvalidateRect(g_hChatFilterBtns[i], nullptr, TRUE);
    }
}

// システムログ表示
static void AddSystemLogDirect(const std::wstring& message) {
    auto now = std::chrono::system_clock::now();
    std::wstring ts = FormatTimestamp(now);
    std::wstring text = L"[" + ts + L"] " + message + L"\r\n";

    // バッファに追加
    ChatBufferEntry entry;
    entry.isSystem = true;
    entry.systemText = text;
    g_chatBuffer.push_back(std::move(entry));
    if ((int)g_chatBuffer.size() > MAX_CHAT_BUFFER)
        g_chatBuffer.pop_front();

    // 全表示RichEditにのみ追加（システムログは全表示のみ表示）
    AppendChatText(g_hChatEdits[0], text, RGB(128, 128, 128));

    // ステータスバーにも表示
    if (g_hLatestLabel) {
        RECT rc;
        GetWindowRect(g_hLatestLabel, &rc);
        MapWindowPoints(HWND_DESKTOP, GetParent(g_hLatestLabel), (POINT*)&rc, 2);
        InvalidateRect(GetParent(g_hLatestLabel), &rc, TRUE);
        SetWindowTextW(g_hLatestLabel, message.c_str());
    }
}

static void ShowStartupReferenceLinks() {
    if (g_startupLinksShown) return;
    g_startupLinksShown = true;

    std::wstring ts = FormatTimestamp(std::chrono::system_clock::now());
    std::wstring line1 = L"[" + ts + L"] [一般] https://jrs-zest.github.io/JRS_item/ アイテム\r\n";
    std::wstring line2 = L"[" + ts + L"] [一般] https://x.gd/Slv10 モンスタ\r\n";

    // フィルタ状態に関係なく、チャット欄で必ず見えるよう全RichEditへ追加する
    for (int i = 0; i < IDC_CHAT_FILTER_COUNT; i++) {
        if (g_hChatEdits[i]) {
            AppendChatText(g_hChatEdits[i], line1, RGB(90, 90, 90));
            AppendChatText(g_hChatEdits[i], line2, RGB(90, 90, 90));
        }
    }

    if (g_hLatestLabel) {
        SetWindowTextW(g_hLatestLabel, L"https://x.gd/Slv10 モンスタ");
    }
}

// ───────────────────────── チャットログ表示 ─────────────────────────
static void AddChatLogToUI(const ChatMessage& msg, bool soundAlreadyPlayed = false) {
    // ささやき受信音
    if (msg.chatType == ChatType::WhisperRx && g_settings && g_settings->whisperNotificationEnabled && !soundAlreadyPlayed) {
        try { if (g_soundService) g_soundService->PlayWhisperSound(); } catch (...) {}
        soundAlreadyPlayed = true;
    }

    // バッファに追加
    ChatBufferEntry entry;
    entry.isSystem = false;
    entry.chatMsg = msg;
    g_chatBuffer.push_back(std::move(entry));
    if ((int)g_chatBuffer.size() > MAX_CHAT_BUFFER)
        g_chatBuffer.pop_front();

    std::wstring ts = FormatTimestamp(msg.timestamp);
    const wchar_t* prefix = GetChatTypePrefix(msg.chatType);
    COLORREF color = ThemeManager::GetChatTypeColor(msg.chatType);

    std::wstring text;
    if (msg.chatType == ChatType::WhisperTx) {
        text = L"[" + ts + L"] " + prefix + L" to " + msg.senderName + L": " + msg.message + L"\r\n";
    } else if (msg.chatType == ChatType::WhisperRx) {
        text = L"[" + ts + L"] " + prefix + L" from " + msg.senderName + L": " + msg.message + L"\r\n";
    } else if (msg.senderName.empty()) {
        text = L"[" + ts + L"] " + prefix + L" " + msg.message + L"\r\n";
    } else {
        text = L"[" + ts + L"] " + prefix + L" " + msg.senderName + L": " + msg.message + L"\r\n";
    }

    // 全表示 RichEdit (index 0) に追加
    if (msg.chatType == ChatType::WhisperRx) {
        AppendWhisperText(g_hChatEdits[0], text, color);
    } else {
        AppendChatText(g_hChatEdits[0], text, color);
    }
    // 該当カテゴリ RichEdit (index 1-4) にも追加
    int catIdx = ChatTypeFilterIndex(msg.chatType);
    if (catIdx > 0 && catIdx < IDC_CHAT_FILTER_COUNT) {
        if (msg.chatType == ChatType::WhisperRx) {
            AppendWhisperText(g_hChatEdits[catIdx], text, color);
        } else {
            AppendChatText(g_hChatEdits[catIdx], text, color);
        }
    }

    // 最新行をステータスに表示
    std::wstring latest = text;
    while (!latest.empty() && (latest.back() == L'\n' || latest.back() == L'\r'))
        latest.pop_back();
    if (g_hLatestLabel) {
        // 透明背景のため、親ウィンドウ側で旧テキスト領域を再描画してから新テキストを設定
        RECT rc;
        GetWindowRect(g_hLatestLabel, &rc);
        MapWindowPoints(HWND_DESKTOP, GetParent(g_hLatestLabel), (POINT*)&rc, 2);
        InvalidateRect(GetParent(g_hLatestLabel), &rc, TRUE);
        SetWindowTextW(g_hLatestLabel, latest.c_str());
    }

    // チャットタブに★マーク
    if (g_hNotebook) {
        int sel = TabCtrl_GetCurSel(g_hNotebook);
        int chatIdx = -1;
        for (int i = 0; i < (int)g_visibleTabs.size(); i++) {
            if (g_visibleTabs[i].name == L"チャット") { chatIdx = i; break; }
        }
        if (chatIdx >= 0 && sel != chatIdx && !g_chatTabHasUnread) {
            g_chatTabHasUnread = true;
            // タブヘッダのみ再描画（TabCtrl_SetItemは全体を無効化するため使わない）
            RECT itemRect;
            if (TabCtrl_GetItemRect(g_hNotebook, chatIdx, &itemRect))
                InvalidateRect(g_hNotebook, &itemRect, FALSE);
        }
    }
}

// ───────────────────────── ツールバーレイアウト ─────────────────────────
static void LayoutToolbar(HWND /*hwnd*/, int clientWidth) {
    // 停止・再開ボタン（左端）
    int x = 4;
    if (g_hBtnStop)   MoveWindow(g_hBtnStop,  x, 3, 40, TOOLBAR_BTN_HEIGHT, FALSE);
    x += 42;
    if (g_hBtnStart)  MoveWindow(g_hBtnStart, x, 3, 40, TOOLBAR_BTN_HEIGHT, FALSE);
    x += 46;

    // 未読ラベル
    if (g_hUnseenLabel) MoveWindow(g_hUnseenLabel, x, 3, 60, TOOLBAR_BTN_HEIGHT, FALSE);
    x += 64;
    // 鐘ラベル
    if (g_hBellLabel) MoveWindow(g_hBellLabel, x, 3, 100, TOOLBAR_BTN_HEIGHT, FALSE);

    // 右端ボタン群（右寄せ）
    int rx = clientWidth - 4;
    if (g_hBtnSettings) { rx -= 30; MoveWindow(g_hBtnSettings, rx, 3, 30, TOOLBAR_BTN_HEIGHT, FALSE); }
    if (g_hBtnMaxV)     { rx -= 32; MoveWindow(g_hBtnMaxV,     rx, 3, 30, TOOLBAR_BTN_HEIGHT, FALSE); }
    if (g_hBtnScreen)   { rx -= 32; MoveWindow(g_hBtnScreen,   rx, 3, 30, TOOLBAR_BTN_HEIGHT, FALSE); }
    if (g_hBtnDC)       { rx -= 32; MoveWindow(g_hBtnDC,       rx, 3, 30, TOOLBAR_BTN_HEIGHT, FALSE); }
}

// ───────────────────────── ListView カラム幅比例配分（共通） ─────────────────────────
// 最小幅を保証しつつ余剰を比例配分する。最後の列が残りを吸収。
static void AdjustListViewColumns(HWND hList, const int minWidths[], const int desired[], int nCols) {
    if (!hList) return;
    RECT rc;
    GetClientRect(hList, &rc);
    int avail = rc.right - rc.left - GetSystemMetrics(SM_CXVSCROLL) - 4;
    // 最小幅合計
    int minSum = 0;
    for (int i = 0; i < nCols; i++) minSum += minWidths[i];
    if (avail <= minSum) {
        // ウィンドウが小さすぎる場合は最小幅を適用
        for (int i = 0; i < nCols; i++)
            ListView_SetColumnWidth(hList, i, minWidths[i]);
        return;
    }
    // desired 合計
    int desiredSum = 0;
    for (int i = 0; i < nCols; i++) desiredSum += desired[i];
    // 比例配分（最小幅保証）
    int remaining = avail;
    for (int i = 0; i < nCols - 1; i++) {
        int w = desired[i] * avail / desiredSum;
        if (w < minWidths[i]) w = minWidths[i];
        ListView_SetColumnWidth(hList, i, w);
        remaining -= w;
    }
    // 最終列は残り全てを使う
    int lastW = (remaining > minWidths[nCols - 1]) ? remaining : minWidths[nCols - 1];
    ListView_SetColumnWidth(hList, nCols - 1, lastW);
}

static void AdjustItemColumns() {
    //                         時刻  アイテム名  OP1  OP2  OP3
    static const int minW[] = { 40, 100, 60, 60, 60 };
    static const int desW[] = { 50, 140, 120, 120, 120 };
    AdjustListViewColumns(g_hItemList, minW, desW, 5);
}

static void AdjustRateRotenColumns() {
    //                         時刻  名称  個数  OP1  OP2  OP3  価格  種別
    static const int minW[] = { 40, 120, 30, 60, 60, 60, 60, 40 };
    static const int desW[] = { 60, 260, 50, 140, 140, 140, 120, 80 };
    AdjustListViewColumns(g_hRateRotenList, minW, desW, 8);
}

// ───────────────────────── タブ表示更新 ─────────────────────────
static void UpdateTabVisibility() {
    if (!g_hNotebook) return;

    // 既存タブを全削除
    TabCtrl_DeleteAllItems(g_hNotebook);
    // 既存ページ非表示
    for (auto& t : g_visibleTabs) {
        if (t.hPage) ShowWindow(t.hPage, SW_HIDE);
    }
    g_visibleTabs.clear();

    // マスタ順にselectedTabsに含まれるもののみ追加
    int idx = 0;
    for (auto& name : g_masterTabOrder) {
        bool found = false;
        for (auto& s : g_selectedTabs) {
            if (s == name) { found = true; break; }
        }
        if (!found) continue;

        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<wchar_t*>(name.c_str());
        TabCtrl_InsertItem(g_hNotebook, idx, &ti);

        TabPageInfo tpi;
        tpi.name = name;
        tpi.hPage = nullptr; // ページウィンドウは後で管理
        g_visibleTabs.push_back(tpi);
        idx++;
    }

    if (idx > 0) TabCtrl_SetCurSel(g_hNotebook, 0);
}

// タブ切り替え時のコンテンツ表示/非表示
static void OnTabSelChanged() {
    if (!g_hNotebook) return;
    int sel = TabCtrl_GetCurSel(g_hNotebook);

    // チャットタブ選択時に★を消す
    for (int i = 0; i < (int)g_visibleTabs.size(); i++) {
        if (g_visibleTabs[i].name == L"チャット" && sel == i && g_chatTabHasUnread) {
            g_chatTabHasUnread = false;
            // タブヘッダのみ再描画（TabCtrl_SetItemは全体を無効化するため使わない）
            RECT itemRect;
            if (TabCtrl_GetItemRect(g_hNotebook, i, &itemRect))
                InvalidateRect(g_hNotebook, &itemRect, FALSE);
        }
    }

    // コンテンツの表示/非表示
    std::wstring selectedName;
    if (sel >= 0 && sel < (int)g_visibleTabs.size()) {
        selectedName = g_visibleTabs[sel].name;
    }

    bool showChat = (selectedName == L"チャット");
    bool showItem = (selectedName == L"アイテム");
    bool showRateRoten = (selectedName == L"露店価格");
    bool showRoten = (selectedName == L"露店売買");
    bool showGold = (selectedName == L"収入");
    bool showDrop = (selectedName == L"ドロップ");
    bool showEquip = (selectedName == L"装備");
    bool showGuild = (selectedName == L"G員");

    // チャットRichEdit: showChat時はアクティブフィルタのみ表示、それ以外は全て非表示
    for (int i = 0; i < IDC_CHAT_FILTER_COUNT; i++) {
        if (g_hChatEdits[i]) {
            bool show = showChat && (i == g_chatFilterActive);
            ShowWindow(g_hChatEdits[i], show ? SW_SHOW : SW_HIDE);
        }
    }
    // チャットフィルタボタン
    for (int i = 0; i < IDC_CHAT_FILTER_COUNT; i++) {
        if (g_hChatFilterBtns[i]) {
            ShowWindow(g_hChatFilterBtns[i], showChat ? SW_SHOW : SW_HIDE);
            if (showChat) InvalidateRect(g_hChatFilterBtns[i], nullptr, TRUE);
        }
    }
    if (g_hChatAutoScroll) ShowWindow(g_hChatAutoScroll, showChat ? SW_SHOW : SW_HIDE);
    if (g_hItemList)       ShowWindow(g_hItemList, showItem ? SW_SHOW : SW_HIDE);
    if (g_hItemEventLog)   ShowWindow(g_hItemEventLog, showItem ? SW_SHOW : SW_HIDE);
    if (g_hItemEventTitle) ShowWindow(g_hItemEventTitle, showItem ? SW_SHOW : SW_HIDE);
    if (g_hItemNotifyBtn)  ShowWindow(g_hItemNotifyBtn, showItem ? SW_SHOW : SW_HIDE);
    if (g_hItemShowAll)    ShowWindow(g_hItemShowAll, showItem ? SW_SHOW : SW_HIDE);
    if (g_hRateRotenList)      ShowWindow(g_hRateRotenList, showRateRoten ? SW_SHOW : SW_HIDE);
    if (g_hRateRotenViewerBtn) ShowWindow(g_hRateRotenViewerBtn, showRateRoten ? SW_SHOW : SW_HIDE);
    if (g_hRotenEdit)       ShowWindow(g_hRotenEdit, showRoten ? SW_SHOW : SW_HIDE);
    if (g_hRotenSnapBtn)    ShowWindow(g_hRotenSnapBtn, showRoten ? SW_SHOW : SW_HIDE);
    if (g_hRotenViewerBtn)  ShowWindow(g_hRotenViewerBtn, showRoten ? SW_SHOW : SW_HIDE);
    // ドロップタブ
    if (g_hDropList)    ShowWindow(g_hDropList, showDrop ? SW_SHOW : SW_HIDE);
    if (g_hDropShowAll) ShowWindow(g_hDropShowAll, showDrop ? SW_SHOW : SW_HIDE);
    if (g_hDropDcCombo) ShowWindow(g_hDropDcCombo, showDrop ? SW_SHOW : SW_HIDE);
    if (g_hDropLblAll)  ShowWindow(g_hDropLblAll, showDrop ? SW_SHOW : SW_HIDE);
    if (g_hDropLblDc)   ShowWindow(g_hDropLblDc, showDrop ? SW_SHOW : SW_HIDE);
    // 装備タブ
    if (g_hEquipList)    ShowWindow(g_hEquipList, showEquip ? SW_SHOW : SW_HIDE);
    if (g_hEquipCopyBtn) ShowWindow(g_hEquipCopyBtn, showEquip ? SW_SHOW : SW_HIDE);
    if (g_hEquipImgBtn)  ShowWindow(g_hEquipImgBtn, showEquip ? SW_SHOW : SW_HIDE);
    if (g_hEquipLabel)   ShowWindow(g_hEquipLabel, showEquip ? SW_SHOW : SW_HIDE);
    // G員タブ
    if (g_hGuildList)    ShowWindow(g_hGuildList, showGuild ? SW_SHOW : SW_HIDE);
    if (g_hGuildCompareBtn) ShowWindow(g_hGuildCompareBtn, showGuild ? SW_SHOW : SW_HIDE);
    if (g_hGuildIntervalLabel) ShowWindow(g_hGuildIntervalLabel, showGuild ? SW_SHOW : SW_HIDE);
    if (g_hGuildIntervalCombo) ShowWindow(g_hGuildIntervalCombo, showGuild ? SW_SHOW : SW_HIDE);
    if (g_hGuildManualSaveBtn) ShowWindow(g_hGuildManualSaveBtn, showGuild ? SW_SHOW : SW_HIDE);
    if (g_hGuildHelpBtn) ShowWindow(g_hGuildHelpBtn, showGuild ? SW_SHOW : SW_HIDE);
    // 収入タブ
    if (g_hGoldBtnStart)    ShowWindow(g_hGoldBtnStart, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldBtnStop)     ShowWindow(g_hGoldBtnStop, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldBtnReset)    ShowWindow(g_hGoldBtnReset, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldBtnSave)     ShowWindow(g_hGoldBtnSave, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldMobGroup)    ShowWindow(g_hGoldMobGroup, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldMobCountVal) ShowWindow(g_hGoldMobCountVal, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldIncomeGroup) ShowWindow(g_hGoldIncomeGroup, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldMobVal)      ShowWindow(g_hGoldMobVal, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldPickupVal)   ShowWindow(g_hGoldPickupVal, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldMerchantVal) ShowWindow(g_hGoldMerchantVal, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldTotalVal)    ShowWindow(g_hGoldTotalVal, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldDecompGroup) ShowWindow(g_hGoldDecompGroup, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldDecompVal)   ShowWindow(g_hGoldDecompVal, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldCrystalVal)  ShowWindow(g_hGoldCrystalVal, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldUmuVal)      ShowWindow(g_hGoldUmuVal, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldLblMobText)  ShowWindow(g_hGoldLblMobText, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldLblMob)      ShowWindow(g_hGoldLblMob, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldLblPickup)   ShowWindow(g_hGoldLblPickup, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldLblMerchant) ShowWindow(g_hGoldLblMerchant, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldLblTotal)    ShowWindow(g_hGoldLblTotal, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldLblDecomp)   ShowWindow(g_hGoldLblDecomp, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldLblCrystal)  ShowWindow(g_hGoldLblCrystal, showGold ? SW_SHOW : SW_HIDE);
    if (g_hGoldLblUmu)      ShowWindow(g_hGoldLblUmu, showGold ? SW_SHOW : SW_HIDE);

    // タブ切替時: 表示されるListViewの列幅を同期 + 再描画保証
    if (showItem) {
        AdjustItemColumns();
        if (g_hItemList) InvalidateRect(g_hItemList, nullptr, TRUE);
    }
    if (showRateRoten) {
        AdjustRateRotenColumns();
        if (g_hRateRotenList) InvalidateRect(g_hRateRotenList, nullptr, TRUE);
    }
    if (showChat  && g_hChatEdits[g_chatFilterActive])  InvalidateRect(g_hChatEdits[g_chatFilterActive], nullptr, TRUE);
    if (showRoten && g_hRotenEdit) InvalidateRect(g_hRotenEdit, nullptr, TRUE);
    if (showDrop) {
        AdjustDropColumns();
        RefreshDropListView();
    }
    if (showEquip) {
        AdjustEquipColumns();
        RefreshEquipListView();
    }
    if (showGuild) {
        AdjustGuildColumns();
        RefreshGuildListView();
    }
}

// ───────────────────────── サービス初期化 ─────────────────────────
static void InitializeServices() {
    // AppSettings
    g_settings = std::make_unique<AppSettings>();
    g_settings->EnsureDefaultConfig();
    g_settings->Load();

    // ItemDatLoader + WantedRegex
    g_itemDatLoader = std::make_unique<ItemDatLoader>();
    g_wantedRegex   = std::make_unique<WantedRegexService>();

    // ChatPacketProcessor
    g_chatProcessor = std::make_unique<ChatPacketProcessor>();

    // ItemPacketProcessor
    g_itemProcessor = std::make_unique<ItemPacketProcessor>(*g_itemDatLoader, *g_wantedRegex);

    // SoundNotificationService
    g_soundService = std::make_unique<SoundNotificationService>();

    // ChatLoggerService
    g_chatLogger = std::make_unique<ChatLoggerService>(g_settings->maxLinesPerFile);

    // ItemLoggerService
    g_itemLogger = std::make_unique<ItemLoggerService>(g_settings->maxLinesPerFile);

    // ProcessMonitorService
    g_processMonitor = std::make_unique<ProcessMonitorService>(g_settings->targetServerIP);

    // BellProcessor
    g_bellProcessor = std::make_unique<BellProcessor>(
        // AddChatFunc — bellの通知チャットをUIスレッドへ
        [](const std::wstring& sender, const std::wstring& message) {
            if (!g_hwndMain) return;
            auto* pair = new std::pair<std::wstring, std::wstring>(sender, message);
            PostMessage(g_hwndMain, WM_BELL_CHAT, 0, (LPARAM)pair);
        },
        // UpdateLabelFunc — bellタイマーラベル更新
        [](const std::wstring& text) {
            if (!g_hwndMain) return;
            wchar_t* buf = new wchar_t[text.size() + 1];
            wcscpy_s(buf, text.size() + 1, text.c_str());
            PostMessage(g_hwndMain, WM_BELL_LABEL, 0, (LPARAM)buf);
        },
        *g_soundService,
        *g_settings
    );

    // PacketCaptureService
    g_captureService = std::make_unique<PacketCaptureService>();

    // RateRotenProcessor
    g_rateRotenProcessor = std::make_unique<RateRotenProcessor>(*g_itemDatLoader, g_settings->maxLinesPerFile);

    // RotenProcessor
    g_rotenProcessor = std::make_unique<RotenProcessor>(*g_itemDatLoader, g_settings->maxLinesPerFile);

    // GoldProcessor
    g_goldProcessor = std::make_unique<GoldProcessor>();
    g_goldProcessor->SetSettings(g_settings.get());

    // DCManager
    g_dcManager = std::make_unique<DCManager>(GetExeDir());

    // DropPacketProcessor
    g_dropProcessor = std::make_unique<DropPacketProcessor>(*g_itemDatLoader);
    // SpecialDropProcessor (0x1133 -> 地下界の書)
    g_specialDropProcessor = std::make_unique<SpecialDropProcessor>(*g_itemDatLoader);

    // EquipmentPacketProcessor (0x123D)
    g_equipProcessor = std::make_unique<EquipmentPacketProcessor>(*g_itemDatLoader);

    // GuildMemberProcessor (0x1128 / 0x1203)
    g_guildMemberProcessor = std::make_unique<GuildMemberProcessor>();
}

// コールバック接続
static void WireCallbacks() {
    // ChatPacketProcessor → UI + Logger
    g_chatProcessor->SetCallback([](const ChatMessage& msg) {
        if (!g_hwndMain) return;
        auto* m = new ChatMessage(msg);
        PostMessage(g_hwndMain, WM_CHAT_MSG, 0, (LPARAM)m);
    });

    // ItemPacketProcessor → UI + Logger
    g_itemProcessor->SetItemCallback([](const ItemPickupInfo& info) {
        if (!g_hwndMain) return;
        auto* m = new ItemPickupInfo(info);
        PostMessage(g_hwndMain, WM_ITEM_PICKUP, 0, (LPARAM)m);
    });

    g_itemProcessor->SetInvFullCallback([](){ /* handled by InvFullWorker */ });

    g_itemProcessor->SetSoundFunc([](bool isItem) {
        if (!g_soundService) return;
        if (isItem) g_soundService->PlayItemSound();
        else        g_soundService->PlayOptionSound();
    });

    // ProcessMonitorService → Capture
    g_processMonitor->SetPortsUpdatedCallback([](const std::set<int>& ports, const std::wstring& /*remoteIp*/) {
        if (!g_hwndMain) return;
        auto* p = new std::set<int>(ports);
        PostMessage(g_hwndMain, WM_PORTS_UPDATED, 0, (LPARAM)p);
    });

    // RateRotenProcessor → UI
    g_rateRotenProcessor->SetCallback([](const RateRotenResult& result) {
        if (!g_hwndMain) return;
        auto* r = new RateRotenResult(result);
        PostMessage(g_hwndMain, WM_RATE_ROTEN, 0, (LPARAM)r);
    });

    // RotenProcessor → UI
    g_rotenProcessor->SetLogCallback([](const RotenLogEntry& entry) {
        if (!g_hwndMain) return;
        auto* e = new RotenLogEntry(entry);
        PostMessage(g_hwndMain, WM_ROTEN_LOG, 0, (LPARAM)e);
    });

    // RotenProcessor → ビューアリフレッシュ（パケットトリガーキャプチャ成功時）
    g_rotenProcessor->SetCaptureCallback([]() {
        if (g_hwndMain) PostMessage(g_hwndMain, WM_ROTEN_VIEWER_REFRESH, 0, 0);
    });

    // GoldProcessor → UI
    if (g_goldProcessor) {
        g_goldProcessor->SetUpdateCallback([]() {
            if (g_hwndMain) PostMessage(g_hwndMain, WM_GOLD_UPDATE, 0, 0);
        });
    }

    // DropPacketProcessor → UI
    if (g_dropProcessor) {
        g_dropProcessor->SetDropCallback([](const DropItemInfo& info) {
            if (!g_hwndMain) return;
            auto* m = new DropItemInfo(info);
            PostMessage(g_hwndMain, WM_DROP_ITEM, 0, (LPARAM)m);
        });
    }

    // SpecialDropProcessor → UI (通知判定は既存 DropPacketProcessor の判定を再利用)
    if (g_specialDropProcessor) {
        g_specialDropProcessor->SetDropCallback([](const DropItemInfo& info) {
            if (!g_hwndMain) return;
            auto* m = new DropItemInfo(info);
            if (g_dropProcessor) {
                m->isNotificationTarget = g_dropProcessor->IsNotificationTarget(m->itemId);
            }
            PostMessage(g_hwndMain, WM_DROP_ITEM, 0, (LPARAM)m);
        });
        // デバッグログ: マーカー検出時の16バイトHEXをチャットに出力（HH:MM:ss 形式）
        g_specialDropProcessor->SetDebugCallback([](const std::wstring& hex) {
            if (!g_hwndMain) return;
            ChatMessage* cm = new ChatMessage();
            cm->timestamp = std::chrono::system_clock::now();
            cm->chatType = ChatType::General;
            cm->senderName = L"SpecDrop";
            // メッセージは "HH:MM:ss HEX"
            // FormatTimestamp will be applied when UI renders from timestamp, so store only HEX prefixed by time string
            // But WM_CHAT_MSG handler uses FormatTimestamp(info->timestamp) itself; to match existing chat format, put HEX in message.
            cm->message = hex;
            PostMessage(g_hwndMain, WM_CHAT_MSG, 0, (LPARAM)cm);
        });
    }

    // EquipmentPacketProcessor → UI
    if (g_equipProcessor) {
        g_equipProcessor->SetEquipCallback([](const EquipmentData& data) {
            if (!g_hwndMain) return;
            auto* m = new EquipmentData(data);
            PostMessage(g_hwndMain, WM_EQUIP_DATA, 0, (LPARAM)m);
        });
    }

    // GuildMemberProcessor → UI + CSV保存
    if (g_guildMemberProcessor) {
        g_guildMemberProcessor->SetGuildCallback([](const GuildMemberResult& result) {
            if (!g_hwndMain) return;
            auto* m = new GuildMemberResult(result);
            PostMessage(g_hwndMain, WM_GUILD_MEMBER, 0, (LPARAM)m);
        });
    }
}

// SoundSettings同期
static void SyncSoundSettings() {
    if (!g_soundService || !g_settings) return;
    g_soundService->isItemSoundEnabled       = g_settings->itemNotificationEnabled;
    g_soundService->isOptionSoundEnabled      = g_settings->optionNotificationEnabled;
    g_soundService->isInventoryFullSoundEnabled = g_settings->invFullNotificationEnabled;
    g_soundService->isWhisperSoundEnabled     = g_settings->whisperNotificationEnabled;
    g_soundService->isBellSoundEnabled        = g_settings->bellNotificationEnabled;
    g_soundService->isDropSoundEnabled          = g_settings->dropNotificationEnabled;
    g_soundService->volume = g_settings->soundVolume;
}

// パケット受信ハンドラ（キャプチャスレッドから呼ばれる）
static void OnPacketReceived(const TcpPacketInfo& pkt) {
    const uint8_t* data = pkt.payload.data();
    int len = (int)pkt.payload.size();

    // BellProcessor (UIスレッドのタイマー依存なのでPostMessageで委譲)
    // → BellProcessor::ProcessPacket は内部でタイマー開始のために SetTimer を使うが
    //   SetTimer は任意スレッドから呼べるのでここで直接呼ぶ（hwnd 指定済み）
    //   ただし安全のため PostMessage 経由にする
    try {
        if (g_bellProcessor) {
            // BellProcessor は内部で win32 timer を使うため UI スレッドで呼ぶ必要がある
            // ここではコピーを作って post する
            auto* copy = new std::vector<uint8_t>(pkt.payload);
            PostMessage(g_hwndMain, WM_APP + 11, pkt.isIncoming ? 1 : 0, (LPARAM)copy);
        }
    } catch (...) {}

    // ChatPacketProcessor
    g_chatProcessor->ProcessPacket(data, len, pkt.dstPort, pkt.isIncoming);

    // ItemPacketProcessor
    g_itemProcessor->ProcessPacket(data, len, pkt.isIncoming, pkt.srcPort, pkt.dstPort);

    // インベントリ満杯検出（インライン・2秒クールダウン）
    if (pkt.isIncoming) {
        static constexpr uint16_t INVENTORY_FULL_SC = 70;
        static auto s_lastInvFull = std::chrono::steady_clock::time_point{};
        bool invFull = false;

        // 単体パケット判定
        if (len >= 20) {
            uint16_t cmdId  = *reinterpret_cast<const uint16_t*>(data + 2);
            uint16_t subCmd = *reinterpret_cast<const uint16_t*>(data + 14);
            if (cmdId == 0x1128 && subCmd == 0x1138) {
                uint16_t sc = *reinterpret_cast<const uint16_t*>(data + 18);
                if (sc == INVENTORY_FULL_SC) invFull = true;
            }
        }
        // 結合パケットスキャン
        if (!invFull && len >= 30) {
            for (int i = 12; i < len - 6; i++) {
                if (data[i] == 0x38 && data[i + 1] == 0x11 && i + 4 < len) {
                    uint16_t sc = *reinterpret_cast<const uint16_t*>(data + i + 4);
                    if (sc == INVENTORY_FULL_SC) { invFull = true; break; }
                }
            }
        }
        if (invFull) {
            auto now = std::chrono::steady_clock::now();
            if (now - s_lastInvFull >= std::chrono::seconds(2)) {
                s_lastInvFull = now;
                if (g_soundService) g_soundService->PlayInventoryFullSound();
                if (g_hwndMain) PostMessage(g_hwndMain, WM_INV_FULL, 0, 0);
            }
        }
    }

    // RateRotenProcessor
    if (g_rateRotenProcessor) {
        g_rateRotenProcessor->ProcessPacket(data, len, pkt.isIncoming, pkt.srcPort, pkt.dstPort);
    }

    // RotenProcessor
    if (g_rotenProcessor) {
        g_rotenProcessor->ProcessPacket(data, len, pkt.isIncoming, pkt.srcPort, pkt.dstPort);
    }

    // GoldProcessor
    if (g_goldProcessor) {
        g_goldProcessor->ProcessPacket(data, len, pkt.isIncoming, pkt.srcPort, pkt.dstPort);
    }

    // DropPacketProcessor
    if (g_dropProcessor) {
        g_dropProcessor->ProcessPacket(data, len, pkt.isIncoming, pkt.srcPort, pkt.dstPort);
    }
    // SpecialDropProcessor
    if (g_specialDropProcessor) {
        g_specialDropProcessor->ProcessPacket(data, len, pkt.isIncoming, pkt.srcPort, pkt.dstPort);
    }

    // EquipmentPacketProcessor (0x123D)
    if (g_equipProcessor) {
        g_equipProcessor->ProcessPacket(data, len, pkt.isIncoming, pkt.srcPort, pkt.dstPort);
    }

    // GuildMemberProcessor (0x1128 / 0x1203)
    if (g_guildMemberProcessor) {
        g_guildMemberProcessor->ProcessPacket(data, len, pkt.isIncoming, pkt.srcPort, pkt.dstPort);
    }
}

// キャプチャ開始
static void StartCapture() {
    if (g_running.load()) return;
    if (!g_settings || !g_captureService) return;

    std::wstring ifaceName = g_settings->interfaceName;

    // FriendlyName → GUID 解決
    auto interfaces = NetworkHelper::GetNetworkInterfaces();
    std::string deviceGuid;

    if (!ifaceName.empty()) {
        for (auto& iface : interfaces) {
            if (iface.name == ifaceName) {
                deviceGuid = iface.guid;
                break;
            }
        }
    }

    // 設定の FriendlyName が現在のアダプタに見つからない場合
    // (旧 Description 形式の設定ファイル or アダプタ変更)
    // → デフォルトゲートウェイから自動検出し、設定を上書き保存
    if (deviceGuid.empty()) {
        auto defaultName = NetworkHelper::GetDefaultGatewayInterfaceName();
        if (!defaultName.empty()) {
            for (auto& iface : interfaces) {
                if (iface.name == defaultName) {
                    deviceGuid = iface.guid;
                    g_settings->SaveInterfaceConfig(defaultName);
                    PostSystemLog(g_hwndMain,
                        L"ネットワークインターフェースを自動検出: " + defaultName);
                    break;
                }
            }
        }
    }

    if (deviceGuid.empty()) {
        PostSystemLog(g_hwndMain, L"ネットワークインターフェースが未設定です");
        return;
    }

    std::string serverIP;
    serverIP.reserve(g_settings->targetServerIP.size());
    for (wchar_t wc : g_settings->targetServerIP) serverIP += static_cast<char>(wc);

    g_captureService->Start(OnPacketReceived, deviceGuid, serverIP, g_hwndMain);
    g_running = true;

    if (g_hwndMain) PostMessage(g_hwndMain, WM_CAPTURE_STATE, 1, 0);
}

static void StopCapture() {
    if (!g_running.load()) return;
    if (g_captureService) g_captureService->Stop();
    g_running = false;
    if (g_hwndMain) PostMessage(g_hwndMain, WM_CAPTURE_STATE, 0, 0);
}

// item.dat 非同期読み込み
static void LoadItemDataAsync() {
    std::thread([]() {
        try {
            if (g_wantedRegex) g_wantedRegex->Load();

            std::wstring datPath = ItemDatLoader::GetItemDatPathFromRegistry();
            if (!datPath.empty() && g_itemDatLoader && g_itemDatLoader->LoadItemDat(datPath)) {
                std::wstring msg = L"item.dat読み込み完了: " + std::to_wstring(g_itemDatLoader->ItemCount()) +
                    L"アイテム, " + std::to_wstring(g_itemDatLoader->OptionCount()) + L"オプション";
                PostSystemLog(g_hwndMain, msg);
                if (g_wantedRegex) {
                    PostSystemLog(g_hwndMain, L"通知設定: " + std::to_wstring(g_wantedRegex->Count()) + L"パターン読み込み");
                }
                // ドロップ通知用 DC.dat 読み込み
                if (g_dropProcessor && g_settings) {
                    std::wstring exeDir = GetExeDir();
                    g_dropProcessor->LoadDropDcDat(exeDir, g_settings->dropDcFolder);
                    PostSystemLog(g_hwndMain, L"Drop DC.dat読み込み完了 (" + g_settings->dropDcFolder + L")");
                }
            } else {
                PostSystemLog(g_hwndMain, L"item.dat読み込み失敗（アイテム名はシリアル番号で表示）");
            }
        } catch (...) {
            PostSystemLog(g_hwndMain, L"item.dat読み込みエラー");
        }

        // 読み込み完了 → auto start
        if (g_hwndMain) PostMessage(g_hwndMain, WM_AUTO_START, 0, 0);
    }).detach();
}

// 設定ダイアログ表示
static void ShowSettingsDialog(HWND hwndParent) {
    if (!g_settings || !g_processMonitor || !g_soundService) return;

    auto previousSelected = g_selectedTabs;

    SettingsDialog dlg(
        *g_settings,
        *g_processMonitor,
        *g_soundService,
        [](const std::wstring& msg) { if (g_hwndMain) PostSystemLog(g_hwndMain, msg); },
        g_masterTabOrder,
        g_selectedTabs
    );
    dlg.Show(hwndParent);

    // サウンド設定再同期
    SyncSoundSettings();

    // タブ設定変更チェック
    auto newSelected = dlg.GetSelectedTabs();
    if (newSelected != previousSelected) {
        g_selectedTabs = newSelected;
        g_settings->selectedTabs = L"";
        for (size_t i = 0; i < g_selectedTabs.size(); i++) {
            if (i > 0) g_settings->selectedTabs += L",";
            g_settings->selectedTabs += g_selectedTabs[i];
        }
        g_settings->Save();
        UpdateTabVisibility();
        OnTabSelChanged();
    }
}

// 縦最大化トグル
static void ToggleVerticalMaximize() {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR hMon = MonitorFromWindow(g_hwndMain, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfo(hMon, &mi);
    RECT workArea = mi.rcWork;

    if (!g_isVerticalMaximized) {
        RECT rc;
        GetWindowRect(g_hwndMain, &rc);
        g_savedHeight = rc.bottom - rc.top;
        g_savedY = rc.top;
        SetWindowPos(g_hwndMain, nullptr, rc.left, workArea.top, rc.right - rc.left, workArea.bottom - workArea.top,
            SWP_NOZORDER);
        g_isVerticalMaximized = true;
        if (g_hBtnMaxV) SetWindowTextW(g_hBtnMaxV, L"元");
    } else {
        RECT rc;
        GetWindowRect(g_hwndMain, &rc);
        SetWindowPos(g_hwndMain, nullptr, rc.left, g_savedY, rc.right - rc.left, g_savedHeight,
            SWP_NOZORDER);
        g_isVerticalMaximized = false;
        if (g_hBtnMaxV) SetWindowTextW(g_hBtnMaxV, L"縦");
    }
}

// EventLog 表示（アイテムタブ）
static void DisplayItemEventLog(const std::wstring& message, bool useBlink = false) {
    if (!g_hItemEventLog) return;
    KillTimer(g_hwndMain, TIMER_EVENTLOG_BLINK);
    KillTimer(g_hwndMain, TIMER_EVENTLOG_CLEAR);

    SetWindowTextW(g_hItemEventLog, message.c_str());
    g_eventLogLastMessage = message;

    if (useBlink && message.find(L"インベントリ満杯") != std::wstring::npos) {
        // 5秒間点滅 (0.25秒間隄20回色を切り替える)
        g_eventLogBlinkCount = 20;
        g_eventLogBlinkOn = true;
        SetTimer(g_hwndMain, TIMER_EVENTLOG_BLINK, 250, nullptr);
    } else {
        // 通常表示: 濃いグレーで5秒間表示後クリア
        g_eventLogBlinkCount = 0;
        g_eventLogBlinkOn = false;
        InvalidateRect(g_hItemEventLog, nullptr, TRUE);
        SetTimer(g_hwndMain, TIMER_EVENTLOG_CLEAR, 5000, nullptr);
    }
}

// ジオメトリ適用
static void ApplyGeometry(HWND hwnd) {
    if (!g_settings || g_settings->geometry.empty()) return;
    // "600x400+100+100"
    int w, h, x, y;
    if (swscanf_s(g_settings->geometry.c_str(), L"%dx%d+%d+%d", &w, &h, &x, &y) == 4) {
        SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER);
    }
}

static std::wstring GetGeometry(HWND hwnd) {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    wchar_t buf[128];
    swprintf_s(buf, L"%dx%d+%d+%d", rc.right - rc.left, rc.bottom - rc.top, rc.left, rc.top);
    return buf;
}

// ───────────────────────── FramedStatic サブクラス ─────────────────────────
static LRESULT CALLBACK FramedStatic_SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        COLORREF defaultBack = RGB(255, 248, 250);
        COLORREF bellActiveBack = RGB(255, 255, 180);
        COLORREF back = defaultBack;
        if (uIdSubclass == IDC_BELL && g_bellActive.load()) back = bellActiveBack;

        HBRUSH brush = CreateSolidBrush(back);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(200, 120, 140));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);

        wchar_t textbuf[256];
        GetWindowTextW(hwnd, textbuf, _countof(textbuf));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(60, 60, 60));
        if (g_uiFont) SelectObject(hdc, g_uiFont);
        DrawTextW(hdc, textbuf, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, FramedStatic_SubclassProc, uIdSubclass);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ───────────────────────── ToolTip ヘルパー ─────────────────────────
static void AddToolTip(HWND hToolTip, HWND hCtrl, const wchar_t* text) {
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd = GetParent(hCtrl);
    ti.uId = (UINT_PTR)hCtrl;
    ti.lpszText = const_cast<wchar_t*>(text);
    SendMessage(hToolTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
}

// ───────────────────────── WndProc ─────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hwndMain = hwnd;

        // アイコン設定（小アイコン）
        {
            HICON hIconSm = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
            if (hIconSm) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);
        }

        // GDI+ 初期化
        Gdiplus::GdiplusStartupInput gdipSI;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdipSI, nullptr);

        // フォント作成
        g_uiFont = ThemeManager::CreateDefaultFont();
        g_uiFontBold = ThemeManager::CreateHeaderFont();
        g_logFont = ThemeManager::CreateLogFont();

        // RichEdit DLL ロード
        LoadLibraryW(L"Msftedit.dll");

        // サービス初期化
        InitializeServices();
        WireCallbacks();

        // SelectedTabs のロード
        if (!g_settings->selectedTabs.empty()) {
            std::wstring s = g_settings->selectedTabs;
            size_t start = 0;
            while (start < s.size()) {
                size_t end = s.find(L',', start);
                if (end == std::wstring::npos) end = s.size();
                std::wstring tab = s.substr(start, end - start);
                if (!tab.empty()) g_selectedTabs.push_back(tab);
                start = end + 1;
            }
        }
        if (g_selectedTabs.empty()) {
            g_selectedTabs = {L"チャット", L"アイテム"};
        }

        // ───── ツールバー ─────
        // 停止ボタン
        g_hBtnStop = CreateWindowExW(0, L"BUTTON", L"停止",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 40, TOOLBAR_BTN_HEIGHT, hwnd, (HMENU)IDC_BTN_STOP, nullptr, nullptr);
        EnableWindow(g_hBtnStop, FALSE);

        // 再開ボタン
        g_hBtnStart = CreateWindowExW(0, L"BUTTON", L"再開",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 40, TOOLBAR_BTN_HEIGHT, hwnd, (HMENU)IDC_BTN_START, nullptr, nullptr);

        // 未読ラベル
        g_hUnseenLabel = CreateWindowExW(0, L"STATIC", L"\u2605\uFF1D\u672A\u8AAD",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, 60, TOOLBAR_BTN_HEIGHT, hwnd, (HMENU)IDC_UNSEEN, nullptr, nullptr);
        SetWindowSubclass(g_hUnseenLabel, FramedStatic_SubclassProc, IDC_UNSEEN, 0);

        // 鐘ラベル
        g_hBellLabel = CreateWindowExW(0, L"STATIC", L"\u9418\u30BF\u30A4\u30DE\u30FC [--:--]",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            0, 0, 100, TOOLBAR_BTN_HEIGHT, hwnd, (HMENU)IDC_BELL, nullptr, nullptr);
        SetWindowSubclass(g_hBellLabel, FramedStatic_SubclassProc, IDC_BELL, 0);

        // DC ボタン
        g_hBtnDC = CreateWindowExW(0, L"BUTTON", L"DC",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 30, TOOLBAR_BTN_HEIGHT, hwnd, (HMENU)IDC_BTN_DC, nullptr, nullptr);

        // 画 ボタン
        g_hBtnScreen = CreateWindowExW(0, L"BUTTON", L"画",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 30, TOOLBAR_BTN_HEIGHT, hwnd, (HMENU)IDC_BTN_SCREEN, nullptr, nullptr);

        // 縦 ボタン
        g_hBtnMaxV = CreateWindowExW(0, L"BUTTON", L"縦",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 30, TOOLBAR_BTN_HEIGHT, hwnd, (HMENU)IDC_BTN_MAXV, nullptr, nullptr);

        // 設定ボタン（歯車アイコンまたはテキスト）
        g_hBtnSettings = CreateWindowExW(0, L"BUTTON", L"⚙",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 30, TOOLBAR_BTN_HEIGHT, hwnd, (HMENU)IDC_BTN_SETTINGS, nullptr, nullptr);
        // settings.png をリソースから GDI+ Bitmap として読み込む
        {
            HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_SETTINGS_PNG), RT_RCDATA);
            if (hRes) {
                HGLOBAL hData = LoadResource(nullptr, hRes);
                DWORD size = SizeofResource(nullptr, hRes);
                if (hData && size) {
                    void* pData = LockResource(hData);
                    IStream* pStream = nullptr;
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
                    if (hMem) {
                        memcpy(GlobalLock(hMem), pData, size);
                        GlobalUnlock(hMem);
                        if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
                            auto* bmp = new Gdiplus::Bitmap(pStream);
                            if (bmp->GetLastStatus() == Gdiplus::Ok) {
                                g_settingsBitmap = bmp;
                            } else {
                                delete bmp;
                            }
                            pStream->Release();
                        } else {
                            GlobalFree(hMem);
                        }
                    }
                }
            }
        }

        // ToolTip
        g_hToolTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        AddToolTip(g_hToolTip, g_hBtnDC, L"ドロップクリーン設定");
        AddToolTip(g_hToolTip, g_hBtnScreen, L"画面サイズ変更");
        AddToolTip(g_hToolTip, g_hBtnMaxV, L"アプリ縦長表示");
        AddToolTip(g_hToolTip, g_hBtnSettings, L"設定");

        // ───── タブコントロール ─────
        g_hNotebook = CreateWindowExW(0, WC_TABCONTROLW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | TCS_OWNERDRAWFIXED,
            0, TOOLBAR_HEIGHT, 600, 350, hwnd, (HMENU)IDC_NOTEBOOK, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hNotebook, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        TabCtrl_SetItemSize(g_hNotebook, 80, 24);

        UpdateTabVisibility();

        // ───── チャットタブ内容: RichEdit ×5（フィルタ別） ─────
        for (int i = 0; i < IDC_CHAT_FILTER_COUNT; i++) {
            g_hChatEdits[i] = CreateWindowExW(0, MSFTEDIT_CLASS, nullptr,
                WS_CHILD | (i == 0 ? WS_VISIBLE : 0) | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                0, 0, 100, 100, hwnd, (HMENU)(INT_PTR)(IDC_CHAT_EDIT + i), nullptr, nullptr);
            if (g_logFont) SendMessage(g_hChatEdits[i], WM_SETFONT, (WPARAM)g_logFont, TRUE);
            SendMessage(g_hChatEdits[i], EM_SETBKGNDCOLOR, 0, ThemeManager::LogBackgroundColor);
            SendMessage(g_hChatEdits[i], EM_SETEVENTMASK, 0, ENM_LINK);
            SendMessage(g_hChatEdits[i], EM_AUTOURLDETECT, TRUE, 0);
        }

        // ───── チャットフィルタボタン ─────
        g_chatFilterFont = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            SHIFTJIS_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");
        for (int i = 0; i < IDC_CHAT_FILTER_COUNT; i++) {
            g_hChatFilterBtns[i] = CreateWindowExW(0, L"BUTTON", g_chatFilterLabels[i],
                WS_CHILD | BS_OWNERDRAW,
                0, 0, 10, CHAT_FILTER_ROW_HEIGHT, hwnd,
                (HMENU)(INT_PTR)(IDC_CHAT_FILTER_ALL + i), nullptr, nullptr);
            if (g_chatFilterFont) SendMessage(g_hChatFilterBtns[i], WM_SETFONT, (WPARAM)g_chatFilterFont, TRUE);
        }

        // AutoScroll チェックボックス
        g_hChatAutoScroll = CreateWindowExW(0, L"BUTTON", L"AutoScroll",
            WS_CHILD | BS_AUTOCHECKBOX,
            0, 0, 80, CHAT_FILTER_ROW_HEIGHT, hwnd, (HMENU)IDC_CHAT_AUTOSCROLL, nullptr, nullptr);
        if (g_chatFilterFont) SendMessage(g_hChatAutoScroll, WM_SETFONT, (WPARAM)g_chatFilterFont, TRUE);
        SendMessage(g_hChatAutoScroll, BM_SETCHECK, BST_CHECKED, 0);

        // ───── アイテムタブ内容 ─────
        // 通知設定ボタン
        g_hItemNotifyBtn = CreateWindowExW(0, L"BUTTON", L"通知設定",
            WS_CHILD | BS_OWNERDRAW,
            4, 0, 80, 24, hwnd, (HMENU)IDC_ITEM_NOTIFY_BTN, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hItemNotifyBtn, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // 全表示チェックボックス
        g_hItemShowAll = CreateWindowExW(0, L"BUTTON", L"全表示",
            WS_CHILD | BS_AUTOCHECKBOX,
            90, 3, 60, 20, hwnd, (HMENU)IDC_ITEM_SHOW_ALL, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hItemShowAll, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // Event Log タイトル
        g_hItemEventTitle = CreateWindowExW(0, L"STATIC", L"Event Log:",
            WS_CHILD | SS_LEFT,
            160, 4, 70, 16, hwnd, (HMENU)IDC_EVENT_LOG_TITLE, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hItemEventTitle, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // Event Log ラベル
        g_hItemEventLog = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | SS_LEFT,
            240, 4, 400, 20, hwnd, (HMENU)IDC_EVENT_LOG, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hItemEventLog, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // ListView (アイテム一覧)
        g_hItemList = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 100, 100, hwnd, (HMENU)IDC_ITEM_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_hItemList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        if (g_uiFont) SendMessage(g_hItemList, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // ListView 列追加
        {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;

            col.pszText = const_cast<wchar_t*>(L"時刻");  col.cx = 50;
            ListView_InsertColumn(g_hItemList, 0, &col);
            col.pszText = const_cast<wchar_t*>(L"アイテム名"); col.cx = 140;
            ListView_InsertColumn(g_hItemList, 1, &col);
            col.pszText = const_cast<wchar_t*>(L"OP1"); col.cx = 120;
            ListView_InsertColumn(g_hItemList, 2, &col);
            col.pszText = const_cast<wchar_t*>(L"OP2"); col.cx = 120;
            ListView_InsertColumn(g_hItemList, 3, &col);
            col.pszText = const_cast<wchar_t*>(L"OP3"); col.cx = 120;
            ListView_InsertColumn(g_hItemList, 4, &col);
        }

        // ───── 露店価格タブ内容 ─────
        // Viewer ボタン
        g_hRateRotenViewerBtn = CreateWindowExW(0, L"BUTTON", L"露店価格Viewer",
            WS_CHILD | BS_OWNERDRAW,
            4, 0, 120, 24, hwnd, (HMENU)IDC_RATE_ROTEN_VIEWER_BTN, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hRateRotenViewerBtn, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // ListView (露店価格一覧)
        g_hRateRotenList = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
            WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 100, 100, hwnd, (HMENU)IDC_RATE_ROTEN_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_hRateRotenList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        if (g_uiFont) SendMessage(g_hRateRotenList, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // 露店価格 ListView 列追加
        {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;

            col.pszText = const_cast<wchar_t*>(L"時刻");   col.cx = 60;
            ListView_InsertColumn(g_hRateRotenList, 0, &col);
            col.pszText = const_cast<wchar_t*>(L"名称");   col.cx = 260;
            ListView_InsertColumn(g_hRateRotenList, 1, &col);
            col.pszText = const_cast<wchar_t*>(L"個数");   col.cx = 50;
            ListView_InsertColumn(g_hRateRotenList, 2, &col);
            col.pszText = const_cast<wchar_t*>(L"OP1");  col.cx = 140;
            ListView_InsertColumn(g_hRateRotenList, 3, &col);
            col.pszText = const_cast<wchar_t*>(L"OP2");  col.cx = 140;
            ListView_InsertColumn(g_hRateRotenList, 4, &col);
            col.pszText = const_cast<wchar_t*>(L"OP3");  col.cx = 140;
            ListView_InsertColumn(g_hRateRotenList, 5, &col);
            col.pszText = const_cast<wchar_t*>(L"価格");   col.cx = 120;
            ListView_InsertColumn(g_hRateRotenList, 6, &col);
            col.pszText = const_cast<wchar_t*>(L"種別");   col.cx = 80;
            ListView_InsertColumn(g_hRateRotenList, 7, &col);
        }
        AdjustRateRotenColumns();

        // ───── 露店売買タブ内容 ─────
        // Snap ボタン
        g_hRotenSnapBtn = CreateWindowExW(0, L"BUTTON", L"Snap",
            WS_CHILD | BS_OWNERDRAW,
            4, 0, 60, 24, hwnd, (HMENU)IDC_ROTEN_SNAP_BTN, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hRotenSnapBtn, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // 露店開始画像を表示ボタン
        g_hRotenViewerBtn = CreateWindowExW(0, L"BUTTON", L"露店開始画像を表示",
            WS_CHILD | BS_OWNERDRAW,
            70, 0, 140, 24, hwnd, (HMENU)IDC_ROTEN_VIEWER_BTN, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hRotenViewerBtn, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // RichEdit (露店売買ログ)
        g_hRotenEdit = CreateWindowExW(0, MSFTEDIT_CLASS, nullptr,
            WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 100, 100, hwnd, (HMENU)IDC_ROTEN_EDIT, nullptr, nullptr);
        if (g_logFont) SendMessage(g_hRotenEdit, WM_SETFONT, (WPARAM)g_logFont, TRUE);
        SendMessage(g_hRotenEdit, EM_SETBKGNDCOLOR, 0, ThemeManager::LogBackgroundColor);

        // ───── 収入タブ内容 ─────
        // ボタン行（実行中/停止/リセット/ログ保存）
        g_hGoldBtnStart = CreateWindowExW(0, L"BUTTON", L"実行中",
            WS_CHILD | BS_OWNERDRAW, 0, 0, 80, 24, hwnd, (HMENU)IDC_GOLD_BTN_START, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldBtnStart, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        g_hGoldBtnStop = CreateWindowExW(0, L"BUTTON", L"停止",
            WS_CHILD | BS_OWNERDRAW, 0, 0, 80, 24, hwnd, (HMENU)IDC_GOLD_BTN_STOP, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldBtnStop, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        g_hGoldBtnReset = CreateWindowExW(0, L"BUTTON", L"リセット",
            WS_CHILD | BS_OWNERDRAW, 0, 0, 80, 24, hwnd, (HMENU)IDC_GOLD_BTN_RESET, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldBtnReset, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        g_hGoldBtnSave = CreateWindowExW(0, L"BUTTON", L"ログ保存",
            WS_CHILD | BS_OWNERDRAW, 0, 0, 80, 24, hwnd, (HMENU)IDC_GOLD_BTN_SAVE, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldBtnSave, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // Mob討伐セクション (GroupBox)
        g_hGoldMobGroup = CreateWindowExW(0, L"BUTTON", L"",
            WS_CHILD | BS_GROUPBOX, 0, 0, 100, 40, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldMobGroup, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        // Mob値ラベル（GroupBox の子ではなく hwnd の子として作成）
        g_hGoldLblMobText = CreateWindowExW(0, L"STATIC", L"倒したMob数:",
            WS_CHILD | SS_LEFT, 0, 0, 100, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldLblMobText, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldMobCountVal = CreateWindowExW(0, L"STATIC", L"0 体",
            WS_CHILD | SS_RIGHT, 0, 0, 120, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldMobCountVal, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // 収入セクション (GroupBox)
        g_hGoldIncomeGroup = CreateWindowExW(0, L"BUTTON", L"収入",
            WS_CHILD | BS_GROUPBOX, 0, 0, 100, 100, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldIncomeGroup, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        // 収入内ラベル
        g_hGoldLblMob = CreateWindowExW(0, L"STATIC", L"Mob:", WS_CHILD | SS_LEFT, 0, 0, 60, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldLblMob, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldMobVal = CreateWindowExW(0, L"STATIC", L"0 Gold", WS_CHILD | SS_RIGHT, 0, 0, 160, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldMobVal, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldLblPickup = CreateWindowExW(0, L"STATIC", L"拾得:", WS_CHILD | SS_LEFT, 0, 0, 60, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldLblPickup, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldPickupVal = CreateWindowExW(0, L"STATIC", L"0 Gold", WS_CHILD | SS_RIGHT, 0, 0, 160, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldPickupVal, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldLblMerchant = CreateWindowExW(0, L"STATIC", L"行商:", WS_CHILD | SS_LEFT, 0, 0, 60, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldLblMerchant, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldMerchantVal = CreateWindowExW(0, L"STATIC", L"0 Gold", WS_CHILD | SS_RIGHT, 0, 0, 160, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldMerchantVal, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldLblTotal = CreateWindowExW(0, L"STATIC", L"合計:", WS_CHILD | SS_LEFT, 0, 0, 60, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFontBold) SendMessage(g_hGoldLblTotal, WM_SETFONT, (WPARAM)g_uiFontBold, TRUE);
        g_hGoldTotalVal = CreateWindowExW(0, L"STATIC", L"0 Gold", WS_CHILD | SS_RIGHT, 0, 0, 160, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFontBold) SendMessage(g_hGoldTotalVal, WM_SETFONT, (WPARAM)g_uiFontBold, TRUE);

        // U分解セクション (GroupBox)
        g_hGoldDecompGroup = CreateWindowExW(0, L"BUTTON", L"U分解",
            WS_CHILD | BS_GROUPBOX, 0, 0, 100, 80, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldDecompGroup, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldLblDecomp = CreateWindowExW(0, L"STATIC", L"分解数:", WS_CHILD | SS_LEFT, 0, 0, 60, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldLblDecomp, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldDecompVal = CreateWindowExW(0, L"STATIC", L"0 個", WS_CHILD | SS_RIGHT, 0, 0, 160, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldDecompVal, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldLblCrystal = CreateWindowExW(0, L"STATIC", L"結晶石:", WS_CHILD | SS_LEFT, 0, 0, 60, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldLblCrystal, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldCrystalVal = CreateWindowExW(0, L"STATIC", L"0 個", WS_CHILD | SS_RIGHT, 0, 0, 160, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldCrystalVal, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldLblUmu = CreateWindowExW(0, L"STATIC", L"UMUコイン:", WS_CHILD | SS_LEFT, 0, 0, 80, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldLblUmu, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        g_hGoldUmuVal = CreateWindowExW(0, L"STATIC", L"0 個", WS_CHILD | SS_RIGHT, 0, 0, 160, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGoldUmuVal, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // 収入タブ初期化
        if (g_goldProcessor) g_goldProcessor->StartCollection();
        // 起動時は集計中 → 開始ボタン無効(グレー)、停止ボタン有効
        if (g_hGoldBtnStart) { SetWindowTextW(g_hGoldBtnStart, L"実行中"); EnableWindow(g_hGoldBtnStart, FALSE); }
        if (g_hGoldBtnStop)  { EnableWindow(g_hGoldBtnStop, TRUE); }

        // ───── ドロップタブ内容 ─────
        // "全表示" ラベル
        g_hDropLblAll = CreateWindowExW(0, L"STATIC", L"全表示",
            WS_CHILD | SS_LEFT,
            4, 0, 40, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hDropLblAll, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        // 全表示チェックボックス
        g_hDropShowAll = CreateWindowExW(0, L"BUTTON", nullptr,
            WS_CHILD | BS_AUTOCHECKBOX,
            44, 0, 16, 20, hwnd, (HMENU)IDC_DROP_SHOW_ALL, nullptr, nullptr);
        // "通知用DC" ラベル
        g_hDropLblDc = CreateWindowExW(0, L"STATIC", L"通知用DC",
            WS_CHILD | SS_RIGHT,
            0, 0, 56, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hDropLblDc, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // DCフォルダ選択 ComboBox
        g_hDropDcCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            70, 0, 180, 200, hwnd, (HMENU)IDC_DROP_DC_COMBO, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hDropDcCombo, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        // DCフォルダ列挙
        {
            namespace fs = std::filesystem;
            std::wstring dcDir = GetExeDir() + L"\\DC";
            int selIdx = -1;
            if (fs::is_directory(dcDir)) {
                int idx = 0;
                for (auto& entry : fs::directory_iterator(dcDir)) {
                    if (entry.is_directory()) {
                        std::wstring name = entry.path().filename().wstring();
                        SendMessageW(g_hDropDcCombo, CB_ADDSTRING, 0, (LPARAM)name.c_str());
                        if (g_settings && name == g_settings->dropDcFolder) selIdx = idx;
                        idx++;
                    }
                }
            }
            if (selIdx >= 0) SendMessageW(g_hDropDcCombo, CB_SETCURSEL, selIdx, 0);
        }

        // ListView (ドロップ一覧)
        g_hDropList = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 100, 100, hwnd, (HMENU)IDC_DROP_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_hDropList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        if (g_uiFont) SendMessage(g_hDropList, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // ドロップ ListView 列追加（時刻, アイテム名, 座標）
        {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;

            col.pszText = const_cast<wchar_t*>(L"時刻"); col.cx = 90;
            ListView_InsertColumn(g_hDropList, 0, &col);
            col.pszText = const_cast<wchar_t*>(L"アイテム名"); col.cx = 260;
            ListView_InsertColumn(g_hDropList, 1, &col);
            col.pszText = const_cast<wchar_t*>(L"座標"); col.cx = 120;
            ListView_InsertColumn(g_hDropList, 2, &col);
        }

        // ───── 装備タブ ─────
        // 注意書きラベル
        g_hEquipLabel = CreateWindowExW(0, L"STATIC", L"ログイン直後のパケットで表示されます",
            WS_CHILD | SS_LEFT,
            0, 0, 260, 20, hwnd, nullptr, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hEquipLabel, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // CSVコピーボタン
        g_hEquipCopyBtn = CreateWindowExW(0, L"BUTTON", L"CSVコピー",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 80, 24, hwnd, (HMENU)IDC_EQUIP_COPY_CSV, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hEquipCopyBtn, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        g_hEquipImgBtn = CreateWindowExW(0, L"BUTTON", L"画像保存",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 80, 24, hwnd, (HMENU)IDC_EQUIP_COPY_IMG, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hEquipImgBtn, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // 装備 ListView (仮想リスト)
        g_hEquipList = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 100, 100, hwnd, (HMENU)IDC_EQUIP_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_hEquipList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        if (g_uiFont) SendMessage(g_hEquipList, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // 装備 ListView 列追加 (Slot, Name, OP1, OP2, OP3)
        {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;

            col.pszText = const_cast<wchar_t*>(L"Slot"); col.cx = 55;
            ListView_InsertColumn(g_hEquipList, 0, &col);
            col.pszText = const_cast<wchar_t*>(L"Name"); col.cx = 160;
            ListView_InsertColumn(g_hEquipList, 1, &col);
            col.pszText = const_cast<wchar_t*>(L"OP1"); col.cx = 120;
            ListView_InsertColumn(g_hEquipList, 2, &col);
            col.pszText = const_cast<wchar_t*>(L"OP2"); col.cx = 120;
            ListView_InsertColumn(g_hEquipList, 3, &col);
            col.pszText = const_cast<wchar_t*>(L"OP3"); col.cx = 120;
            ListView_InsertColumn(g_hEquipList, 4, &col);
        }

        // G員 ListView (仮想リスト)
        g_hGuildCompareBtn = CreateWindowExW(0, L"BUTTON", L"比較",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 60, 24, hwnd, (HMENU)IDC_GUILD_COMPARE_BTN, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGuildCompareBtn, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // 自動保存間隔ラベル + ドロップダウン + 手動保存ボタン (右寄せ)
        g_hGuildIntervalLabel = CreateWindowExW(0, L"STATIC", L"現在の自動保存間隔",
            WS_CHILD | SS_RIGHT | SS_CENTERIMAGE,
            0, 0, 130, 24, hwnd, (HMENU)IDC_GUILD_INTERVAL_LABEL, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGuildIntervalLabel, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        g_hGuildIntervalCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 90, 160, hwnd, (HMENU)IDC_GUILD_INTERVAL_COMBO, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGuildIntervalCombo, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
        // 選択肢追加
        SendMessageW(g_hGuildIntervalCombo, CB_ADDSTRING, 0, (LPARAM)L"1日");
        SendMessageW(g_hGuildIntervalCombo, CB_ADDSTRING, 0, (LPARAM)L"3日");
        SendMessageW(g_hGuildIntervalCombo, CB_ADDSTRING, 0, (LPARAM)L"1週間");
        SendMessageW(g_hGuildIntervalCombo, CB_ADDSTRING, 0, (LPARAM)L"1ヶ月");
        // 現在の設定値から選択位置を決定
        {
            int h = g_settings ? g_settings->guildSaveIntervalHours : 72;
            int sel = 1; // デフォルト: 3日
            if (h <= 24) sel = 0;
            else if (h <= 72) sel = 1;
            else if (h <= 168) sel = 2;
            else sel = 3;
            SendMessageW(g_hGuildIntervalCombo, CB_SETCURSEL, sel, 0);
        }

        g_hGuildManualSaveBtn = CreateWindowExW(0, L"BUTTON", L"手動保存",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, 70, 24, hwnd, (HMENU)IDC_GUILD_MANUAL_SAVE, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hGuildManualSaveBtn, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // ヘルプボタン（help.png アイコン）
        g_hGuildHelpBtn = CreateWindowExW(0, L"BUTTON", L"?",
            WS_CHILD | BS_OWNERDRAW,
            0, 0, 24, 24, hwnd, (HMENU)(INT_PTR)IDC_GUILD_HELP_BTN, nullptr, nullptr);
        // help.png をリソースから読み込み
        {
            HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_HELP_PNG), RT_RCDATA);
            if (hRes) {
                HGLOBAL hData = LoadResource(nullptr, hRes);
                DWORD size = SizeofResource(nullptr, hRes);
                if (hData && size) {
                    void* pData = LockResource(hData);
                    IStream* pStream = nullptr;
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
                    if (hMem) {
                        memcpy(GlobalLock(hMem), pData, size);
                        GlobalUnlock(hMem);
                        if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
                            auto* bmp = new Gdiplus::Bitmap(pStream);
                            if (bmp->GetLastStatus() == Gdiplus::Ok) {
                                g_helpBitmap = bmp;
                            } else {
                                delete bmp;
                            }
                            pStream->Release();
                        } else {
                            GlobalFree(hMem);
                        }
                    }
                }
            }
        }

        g_hGuildList = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
            WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 100, 100, hwnd, (HMENU)IDC_GUILD_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_hGuildList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        if (g_uiFont) SendMessage(g_hGuildList, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // G員 ListView 列追加 (ギルド員名, Lv, 職業, 役職)
        {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;

            col.pszText = const_cast<wchar_t*>(L"ギルド員名"); col.cx = 180;
            ListView_InsertColumn(g_hGuildList, 0, &col);
            col.pszText = const_cast<wchar_t*>(L"Lv"); col.cx = 50;
            col.fmt = LVCFMT_RIGHT;
            ListView_InsertColumn(g_hGuildList, 1, &col);
            col.fmt = LVCFMT_LEFT;
            col.pszText = const_cast<wchar_t*>(L"職業"); col.cx = 80;
            ListView_InsertColumn(g_hGuildList, 2, &col);
            col.pszText = const_cast<wchar_t*>(L"役職"); col.cx = 80;
            ListView_InsertColumn(g_hGuildList, 3, &col);
        }

        // ───── ステータスバー（最新チャット表示）─────
        g_hLatestLabel = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS,
            0, 0, 600, STATUS_HEIGHT, hwnd, (HMENU)IDC_LATEST_LABEL, nullptr, nullptr);
        if (g_uiFont) SendMessage(g_hLatestLabel, WM_SETFONT, (WPARAM)g_uiFont, TRUE);

        // ───── ジオメトリ復元 ─────
        ApplyGeometry(hwnd);

        // 初期タブ表示
        OnTabSelChanged();

        // 初期レイアウト
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            LayoutToolbar(hwnd, rc.right);
        }

        // サウンド設定同期
        SyncSoundSettings();

        // サウンド非同期読み込み開始
        if (g_soundService) g_soundService->StartBackgroundLoad();

        // 鐘タイマー復元
        if (g_bellProcessor) {
            g_bellProcessor->SetTimerHwnd(hwnd);
            g_bellProcessor->RestoreFromSaved();
        }

        // 定期クリーンアップタイマー (5秒)
        SetTimer(hwnd, TIMER_CLEANUP, 5000, nullptr);

        // 起動時の参照URLをチャット欄へ表示
        ShowStartupReferenceLinks();

        // item.dat 非同期読み込み → 完了後に自動開始
        LoadItemDataAsync();

        return 0;
    }

    case WM_SIZE:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right;
        int ch = rc.bottom;

        // 全MoveWindowをFALSE（再描画抑制）で実行し、最後に一括再描画
        LayoutToolbar(hwnd, cw);

        // タブコントロール
        int tabTop = TOOLBAR_HEIGHT;
        int tabHeight = ch - TOOLBAR_HEIGHT - STATUS_HEIGHT;
        if (g_hNotebook) MoveWindow(g_hNotebook, 0, tabTop, cw, tabHeight, FALSE);

        // タブ内コンテンツ領域
       
        RECT tabRC;
        if (g_hNotebook) {
            GetClientRect(g_hNotebook, &tabRC);
            TabCtrl_AdjustRect(g_hNotebook, FALSE, &tabRC);
        } else {
            tabRC = {4, tabTop + 30, cw - 4, tabTop + tabHeight - 4};
        }
        int contentX = tabRC.left;
        int contentY = tabRC.top + tabTop;
        int contentW = tabRC.right - tabRC.left;
        int contentH = tabRC.bottom - tabRC.top;

        // 現在の表示タブを取得
        std::wstring curTab;
        if (g_hNotebook) {
            int sel = TabCtrl_GetCurSel(g_hNotebook);
            if (sel >= 0 && sel < (int)g_visibleTabs.size())
                curTab = g_visibleTabs[sel].name;
        }

        // チャットフィルタボタン行 + RichEdit
        {
            int filterY = contentY;
            int filterH = CHAT_FILTER_ROW_HEIGHT;
            int btnX = contentX;
            int btnPad = 3;
            // ボタン幅をテキスト長に合わせて可変
            int btnWidths[IDC_CHAT_FILTER_COUNT] = { 42, 58, 48, 40, 50 };
            for (int i = 0; i < IDC_CHAT_FILTER_COUNT; i++) {
                if (g_hChatFilterBtns[i])
                    MoveWindow(g_hChatFilterBtns[i], btnX, filterY, btnWidths[i], filterH, FALSE);
                btnX += btnWidths[i] + btnPad;
            }
            if (g_hChatAutoScroll)
                MoveWindow(g_hChatAutoScroll, btnX + 6, filterY, 80, filterH, FALSE);
            int chatY = contentY + filterH + 1;
            int chatH = contentH - filterH - 1;
            if (g_hChatEdits[0]) {
                for (int i = 0; i < IDC_CHAT_FILTER_COUNT; i++) {
                    if (g_hChatEdits[i]) MoveWindow(g_hChatEdits[i], contentX, chatY, contentW, chatH, FALSE);
                }
                // サイズ変更後、アクティブなRichEditのスクロール位置を底に再補正
                if (g_chatAutoScroll && g_hChatEdits[g_chatFilterActive])
                    SendMessage(g_hChatEdits[g_chatFilterActive], WM_VSCROLL, SB_BOTTOM, 0);
            }
        }

        // アイテムタブ（上部コントロール + ListView）
        int itemTopPanelH = 30;
        if (g_hItemNotifyBtn)  MoveWindow(g_hItemNotifyBtn, contentX + 4, contentY + 3, 80, 24, FALSE);
        if (g_hItemShowAll)    MoveWindow(g_hItemShowAll,   contentX + 90, contentY + 6, 60, 20, FALSE);
        if (g_hItemEventTitle) MoveWindow(g_hItemEventTitle, contentX + 160, contentY + 7, 70, 16, FALSE);
        if (g_hItemEventLog)   MoveWindow(g_hItemEventLog,  contentX + 240, contentY + 7, contentW - 250, 20, FALSE);
        if (g_hItemList)       MoveWindow(g_hItemList, contentX, contentY + itemTopPanelH, contentW, contentH - itemTopPanelH, FALSE);
        if (curTab == L"アイテム") AdjustItemColumns();

        // 露店価格タブ（Viewerボタン + ListView）
        int rrTopPanelH = 30;
        if (g_hRateRotenViewerBtn) MoveWindow(g_hRateRotenViewerBtn, contentX + (contentW - 120) / 2, contentY + 3, 120, 24, FALSE);
        if (g_hRateRotenList) MoveWindow(g_hRateRotenList, contentX, contentY + rrTopPanelH, contentW, contentH - rrTopPanelH, FALSE);
        if (curTab == L"露店価格") AdjustRateRotenColumns();

        // 露店売買タブ（Snapボタン + 画像表示ボタン + RichEdit）
        int rotenTopH = 30;
        int rotenBtnTotalW = 60 + 6 + 140;
        int rotenBtnX = contentX + (contentW - rotenBtnTotalW) / 2;
        if (g_hRotenSnapBtn)    MoveWindow(g_hRotenSnapBtn, rotenBtnX, contentY + 3, 60, 24, FALSE);
        if (g_hRotenViewerBtn)  MoveWindow(g_hRotenViewerBtn, rotenBtnX + 66, contentY + 3, 140, 24, FALSE);
        if (g_hRotenEdit)       MoveWindow(g_hRotenEdit, contentX, contentY + rotenTopH, contentW, contentH - rotenTopH, FALSE);

        // 収入タブ（壊れる前の正確なレイアウト復元）
        {
            int goldBtnTotalW = 80 * 4 + 6 * 3;
            int goldBtnX = contentX + (contentW - goldBtnTotalW) / 2;
            int goldY = contentY + 3;
            if (g_hGoldBtnStart) MoveWindow(g_hGoldBtnStart, goldBtnX, goldY, 80, 24, FALSE);
            if (g_hGoldBtnStop)  MoveWindow(g_hGoldBtnStop,  goldBtnX + 86, goldY, 80, 24, FALSE);
            if (g_hGoldBtnReset) MoveWindow(g_hGoldBtnReset, goldBtnX + 172, goldY, 80, 24, FALSE);
            if (g_hGoldBtnSave)  MoveWindow(g_hGoldBtnSave,  goldBtnX + 258, goldY, 80, 24, FALSE);

            int secW = contentW * 2 / 3;
            if (secW < 280) secW = 280;  // 最小幅保証
            int secX = contentX + (contentW - secW) / 2;
            int rowH = 20;
            int topPad = 18;
            int botPad = 8;
            int curY = contentY + 30;

            // Mob討伐セクション
            int mobH = topPad + rowH + botPad;
            if (g_hGoldMobGroup)     MoveWindow(g_hGoldMobGroup, secX, curY, secW, mobH, FALSE);
            if (g_hGoldLblMobText)   MoveWindow(g_hGoldLblMobText, secX + 8, curY + 16, 100, rowH, FALSE);
            if (g_hGoldMobCountVal)  MoveWindow(g_hGoldMobCountVal, secX + secW - 128, curY + 16, 120, rowH, FALSE);
            curY += mobH + 6;

            // 収入セクション
            int incomeH = topPad + 4 * rowH + botPad;
            if (g_hGoldIncomeGroup)  MoveWindow(g_hGoldIncomeGroup, secX, curY, secW, incomeH, FALSE);
            if (g_hGoldLblMob)       MoveWindow(g_hGoldLblMob, secX + 8, curY + topPad, 60, rowH, FALSE);
            if (g_hGoldMobVal)       MoveWindow(g_hGoldMobVal, secX + secW - 168, curY + topPad, 160, rowH, FALSE);
            if (g_hGoldLblPickup)    MoveWindow(g_hGoldLblPickup, secX + 8, curY + topPad + rowH, 60, rowH, FALSE);
            if (g_hGoldPickupVal)    MoveWindow(g_hGoldPickupVal, secX + secW - 168, curY + topPad + rowH, 160, rowH, FALSE);
            if (g_hGoldLblMerchant)  MoveWindow(g_hGoldLblMerchant, secX + 8, curY + topPad + rowH * 2, 60, rowH, FALSE);
            if (g_hGoldMerchantVal)  MoveWindow(g_hGoldMerchantVal, secX + secW - 168, curY + topPad + rowH * 2, 160, rowH, FALSE);
            if (g_hGoldLblTotal)     MoveWindow(g_hGoldLblTotal, secX + 8, curY + topPad + rowH * 3, 60, rowH, FALSE);
            if (g_hGoldTotalVal)     MoveWindow(g_hGoldTotalVal, secX + secW - 168, curY + topPad + rowH * 3, 160, rowH, FALSE);
            curY += incomeH + 6;

            // U分解セクション
            int decompH = topPad + 3 * rowH + botPad;
            if (g_hGoldDecompGroup)  MoveWindow(g_hGoldDecompGroup, secX, curY, secW, decompH, FALSE);
            if (g_hGoldLblDecomp)    MoveWindow(g_hGoldLblDecomp, secX + 8, curY + topPad, 60, rowH, FALSE);
            if (g_hGoldDecompVal)    MoveWindow(g_hGoldDecompVal, secX + secW - 168, curY + topPad, 160, rowH, FALSE);
            if (g_hGoldLblCrystal)   MoveWindow(g_hGoldLblCrystal, secX + 8, curY + topPad + rowH, 60, rowH, FALSE);
            if (g_hGoldCrystalVal)   MoveWindow(g_hGoldCrystalVal, secX + secW - 168, curY + topPad + rowH, 160, rowH, FALSE);
            if (g_hGoldLblUmu)       MoveWindow(g_hGoldLblUmu, secX + 8, curY + topPad + rowH * 2, 80, rowH, FALSE);
            if (g_hGoldUmuVal)       MoveWindow(g_hGoldUmuVal, secX + secW - 168, curY + topPad + rowH * 2, 160, rowH, FALSE);
        }
        {
            int dropTopH = 30;
            int lx = contentX + 4;
            if (g_hDropLblAll)   MoveWindow(g_hDropLblAll, lx, contentY + 7, 40, 16, FALSE); lx += 42;
            if (g_hDropShowAll)  MoveWindow(g_hDropShowAll, lx, contentY + 6, 16, 20, FALSE); lx += 26;
            if (g_hDropLblDc)    MoveWindow(g_hDropLblDc, lx, contentY + 7, 56, 16, FALSE); lx += 58;
            if (g_hDropDcCombo)  MoveWindow(g_hDropDcCombo, lx, contentY + 3, 160, 200, FALSE);
            if (g_hDropList)     MoveWindow(g_hDropList, contentX, contentY + dropTopH, contentW, contentH - dropTopH, FALSE);
            if (curTab == L"ドロップ") AdjustDropColumns();
        }

        // 装備タブ（注意書きラベル + CSVコピーボタン + ListView）
        {
            int equipTopH = 30;
            int lx = contentX + 4;
            if (g_hEquipLabel)   MoveWindow(g_hEquipLabel, lx, contentY + 7, 260, 16, FALSE);
            if (g_hEquipImgBtn)  MoveWindow(g_hEquipImgBtn, contentX + contentW - 84, contentY + 3, 80, 24, FALSE);
            if (g_hEquipCopyBtn) MoveWindow(g_hEquipCopyBtn, contentX + contentW - 84 - 84, contentY + 3, 80, 24, FALSE);
            if (g_hEquipList)    MoveWindow(g_hEquipList, contentX, contentY + equipTopH, contentW, contentH - equipTopH, FALSE);
            if (curTab == L"装備") AdjustEquipColumns();
        }

        // G員タブ（比較ボタン + 保存系右寄せ + ListView）
        {
            int guildTopH = 30;
            if (g_hGuildCompareBtn) MoveWindow(g_hGuildCompareBtn, contentX + 4, contentY + 3, 60, 24, FALSE);
            // 右寄せ配置: ヘルプ | 手動保存 | ドロップダウン | ラベル
            int rx = contentX + contentW;
            if (g_hGuildHelpBtn) { rx -= 28; MoveWindow(g_hGuildHelpBtn, rx, contentY + 3, 24, 24, FALSE); }
            if (g_hGuildManualSaveBtn) { rx -= 74; MoveWindow(g_hGuildManualSaveBtn, rx, contentY + 3, 70, 24, FALSE); }
            if (g_hGuildIntervalCombo) { rx -= 94; MoveWindow(g_hGuildIntervalCombo, rx, contentY + 2, 90, 160, FALSE); }
            if (g_hGuildIntervalLabel) { rx -= 134; MoveWindow(g_hGuildIntervalLabel, rx, contentY + 3, 130, 24, FALSE); }
            if (g_hGuildList) MoveWindow(g_hGuildList, contentX, contentY + guildTopH, contentW, contentH - guildTopH, FALSE);
            if (curTab == L"G員") AdjustGuildColumns();
        }

        // ステータスバー
        if (g_hLatestLabel) MoveWindow(g_hLatestLabel, 4, ch - STATUS_HEIGHT, cw - 8, STATUS_HEIGHT, FALSE);

        // 全コントロール配置完了後に一括再描画（子ウィンドウ含む）
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);

        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 400;
        mmi->ptMinTrackSize.y = 300;
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        switch (id) {
        case IDC_CHAT_FILTER_ALL:
        case IDC_CHAT_FILTER_GENERAL:
        case IDC_CHAT_FILTER_PARTY:
        case IDC_CHAT_FILTER_GUILD:
        case IDC_CHAT_FILTER_WHISPER:
            OnChatFilterToggle(id - IDC_CHAT_FILTER_ALL);
            break;
        case IDC_CHAT_AUTOSCROLL:
            g_chatAutoScroll = (SendMessage(g_hChatAutoScroll, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case IDC_BTN_STOP:
            StopCapture();
            break;
        case IDC_BTN_START:
            StartCapture();
            break;
        case IDC_BTN_SETTINGS:
            ShowSettingsDialog(hwnd);
            break;
        case IDC_BTN_DC:
            if (g_dcManager && g_itemDatLoader && g_settings) {
                DCSettingDialog::Show(hwnd, *g_dcManager, *g_settings, *g_itemDatLoader);
                // ダイアログでドロップ通知用DC.datが変更された可能性があるので再読み込み
                if (g_dropProcessor && !g_settings->dropDcFolder.empty()) {
                    g_dropProcessor->LoadDropDcDat(GetExeDir(), g_settings->dropDcFolder);
                }
            }
            break;
        case IDC_BTN_SCREEN:
            ScreenSizeDialog::Show(hwnd);
            break;
        case IDC_RATE_ROTEN_VIEWER_BTN:
        {
            // 露店価格Viewer.exe を起動
            std::wstring viewerPath = GetExeDir() + L"\\露店価格Viewer\\露店価格Viewer.exe";
            if (GetFileAttributesW(viewerPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                ShellExecuteW(hwnd, L"open", viewerPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else {
                MessageBoxW(hwnd, (L"露店価格Viewer.exeが見つかりません:\n" + viewerPath).c_str(), L"エラー", MB_OK | MB_ICONERROR);
            }
            break;
        }
        case IDC_BTN_MAXV:
            ToggleVerticalMaximize();
            break;
        case IDC_ROTEN_SNAP_BTN:
        {
            if (g_rotenProcessor) {
                EnableWindow(g_hRotenSnapBtn, FALSE);
                std::thread([hwnd]() {
                    Sleep(500);
                    bool ok = g_rotenProcessor->CaptureRotenStartArea();
                    PostMessage(hwnd, WM_ROTEN_LOG, 0, ok
                        ? (LPARAM)new RotenLogEntry{L"", L"露店開始画像をキャプチャしました"}
                        : (LPARAM)nullptr);
                    // ビューアが開いていれば画像リストを更新
                    if (ok) PostMessage(hwnd, WM_ROTEN_VIEWER_REFRESH, 0, 0);
                    // Re-enable button on UI thread
                    PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_ROTEN_SNAP_BTN + 9000, 0), 0);
                }).detach();
            }
            break;
        }
        case IDC_ROTEN_SNAP_BTN + 9000:  // re-enable snap button signal
            if (g_hRotenSnapBtn) EnableWindow(g_hRotenSnapBtn, TRUE);
            break;
        case IDC_ROTEN_VIEWER_BTN:
            if (g_rotenProcessor)
                g_rotenProcessor->ToggleImageViewer(hwnd, g_hRotenViewerBtn);
            break;
        case IDC_ITEM_NOTIFY_BTN:
            if (g_wantedRegex) {
                if (WantedRegexDialog::Show(hwnd, *g_wantedRegex)) {
                    g_wantedRegex->Load();
                    PostSystemLog(g_hwndMain, L"通知設定を更新しました: " + std::to_wstring(g_wantedRegex->Count()) + L"パターン");
                }
            }
            break;
        case IDC_GOLD_BTN_START:
            if (g_goldProcessor) g_goldProcessor->StartCollection();
            // 開始ボタン無効(グレー) + テキスト"実行中"、停止ボタン有効
            if (g_hGoldBtnStart) { SetWindowTextW(g_hGoldBtnStart, L"実行中"); EnableWindow(g_hGoldBtnStart, FALSE); }
            if (g_hGoldBtnStop)  { EnableWindow(g_hGoldBtnStop, TRUE); }
            break;
        case IDC_GOLD_BTN_STOP:
            if (g_goldProcessor) g_goldProcessor->StopCollection();
            // 開始ボタン有効 + テキスト"集計開始"、停止ボタン無効(グレー)
            if (g_hGoldBtnStart) { SetWindowTextW(g_hGoldBtnStart, L"集計開始"); EnableWindow(g_hGoldBtnStart, TRUE); }
            if (g_hGoldBtnStop)  { EnableWindow(g_hGoldBtnStop, FALSE); }
            break;
        case IDC_GOLD_BTN_RESET:
            if (g_goldProcessor) g_goldProcessor->ResetCollection();
            break;
        case IDC_GOLD_BTN_SAVE:
            if (g_goldProcessor) g_goldProcessor->SaveIncomeLog();
            break;
        case IDC_ITEM_SHOW_ALL:
            if (code == BN_CLICKED) {
                g_showAllItems = (SendMessage(g_hItemShowAll, BM_GETCHECK, 0, 0) == BST_CHECKED);
                RefreshItemListView();
            }
            break;
        case IDC_DROP_SHOW_ALL:
            if (code == BN_CLICKED) {
                g_showAllDrops = (SendMessage(g_hDropShowAll, BM_GETCHECK, 0, 0) == BST_CHECKED);
                RefreshDropListView();
            }
            break;
        case IDC_DROP_DC_COMBO:
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(g_hDropDcCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    wchar_t buf[256]{};
                    SendMessageW(g_hDropDcCombo, CB_GETLBTEXT, sel, (LPARAM)buf);
                    std::wstring folder = buf;
                    if (g_settings) g_settings->dropDcFolder = folder;
                    if (g_dropProcessor) {
                        g_dropProcessor->LoadDropDcDat(GetExeDir(), folder);
                        PostSystemLog(g_hwndMain, L"Drop DC.dat切替: " + folder);
                    }
                }
            }
            break;
        case IDC_GUILD_COMPARE_BTN:
        {
            namespace fs = std::filesystem;
            auto folder = (fs::path(GetExeDir()) / L"ログ" / L"ギルド員").wstring();
            ShowGuildCompareWindow(GetModuleHandle(nullptr), hwnd, folder, g_uiFont);
            break;
        }
        case IDC_GUILD_INTERVAL_COMBO:
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(g_hGuildIntervalCombo, CB_GETCURSEL, 0, 0);
                int hours = 72; // デフォルト3日
                switch (sel) {
                case 0: hours = 24; break;   // 1日
                case 1: hours = 72; break;   // 3日
                case 2: hours = 168; break;  // 1週間
                case 3: hours = 720; break;  // 1ヶ月
                }
                if (g_settings) {
                    g_settings->guildSaveIntervalHours = hours;
                    g_settings->SaveGuildConfig();
                }
            }
            break;
        case IDC_GUILD_HELP_BTN:
        {
            // リソースからテキストを読み出して一時ファイルに書き出し、notepadで開く
            HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_HELP_TXT), RT_RCDATA);
            if (hRes) {
                HGLOBAL hData = LoadResource(nullptr, hRes);
                DWORD sz = SizeofResource(nullptr, hRes);
                if (hData && sz) {
                    const char* pData = static_cast<const char*>(LockResource(hData));
                    wchar_t tmpDir[MAX_PATH]{};
                    GetTempPathW(MAX_PATH, tmpDir);
                    std::wstring tmpFile = std::wstring(tmpDir) + L"G員タブについて.txt";
                    HANDLE hFile = CreateFileW(tmpFile.c_str(), GENERIC_WRITE, 0, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD written = 0;
                        WriteFile(hFile, pData, sz, &written, nullptr);
                        CloseHandle(hFile);
                        ShellExecuteW(hwnd, L"open", L"notepad.exe", tmpFile.c_str(), nullptr, SW_SHOWNORMAL);
                    }
                }
            }
            break;
        }
        case IDC_GUILD_MANUAL_SAVE:
        {
            if (!g_lastGuildResult.has_value() || g_lastGuildResult->members.empty()) {
                MessageBoxW(hwnd,
                    L"保存できるギルド員データがありません。\n"
                    L"再ログインするかマップ移動してギルド員リストパケットを受信してください。",
                    L"手動保存", MB_OK | MB_ICONINFORMATION);
            } else if (g_guildMemberProcessor) {
                g_guildMemberProcessor->ForceSaveCsv(GetExeDir(), *g_lastGuildResult);
                MessageBoxW(hwnd, L"ギルド員データを保存しました。", L"手動保存", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        case IDC_EQUIP_COPY_CSV:
        {
            if (g_equips.empty()) {
                MessageBoxW(hwnd,
                    L"保存出来る装備データがありません。\n"
                    L"ログイン直後に取得出来るパケット情報を取得してください。",
                    L"装備データなし", MB_OK | MB_ICONWARNING);
                break;
            }
            // 装備データをCSVとしてクリップボードにコピー
            std::wstring csv = L"Slot,Name,OP1,OP2,OP3\r\n";
            for (auto& e : g_equips) {
                csv += e.slotName + L",";
                csv += e.itemName + L",";
                csv += e.opName[0] + L",";
                csv += e.opName[1] + L",";
                csv += e.opName[2] + L"\r\n";
            }
            bool csvCopied = false;
            if (OpenClipboard(hwnd)) {
                EmptyClipboard();
                size_t sz = (csv.size() + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                if (hMem) {
                    wchar_t* p = (wchar_t*)GlobalLock(hMem);
                    if (p) {
                        wcscpy_s(p, csv.size() + 1, csv.c_str());
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                        csvCopied = true;
                    }
                }
                CloseClipboard();
            }
            if (csvCopied) {
                MessageBoxW(hwnd,
                    L"装備情報をCSVコピーしました。\n"
                    L"スプレッドシートなどに貼り付けて\n"
                    L"「テキストを列に分割」を選択してください。",
                    L"CSVコピー完了", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        case IDC_EQUIP_COPY_IMG:
        {
            if (g_equips.empty()) {
                MessageBoxW(hwnd,
                    L"保存出来る装備データがありません。\n"
                    L"ログイン直後に取得出来るパケット情報を取得してください。",
                    L"装備データなし", MB_OK | MB_ICONWARNING);
                break;
            }
            Gdiplus::Bitmap* bmp = RenderEquipTableImage();
            if (!bmp) break;

            // --- 保存先: <EXE>\ログ\装備画像\YYYYMMDD_hhmmss.png ---
            wchar_t exePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::filesystem::path dir = std::filesystem::path(exePath).parent_path() / L"ログ" / L"装備画像";
            std::filesystem::create_directories(dir);

            SYSTEMTIME st{};
            GetLocalTime(&st);
            wchar_t fname[64]{};
            swprintf_s(fname, L"%04d%02d%02d_%02d%02d%02d.png",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            std::wstring pngPath = (dir / fname).wstring();

            // PNG エンコーダで保存
            CLSID pngClsid;
            bool saved = false;
            if (GetEncoderClsid(L"image/png", &pngClsid) >= 0) {
                saved = (bmp->Save(pngPath.c_str(), &pngClsid, nullptr) == Gdiplus::Ok);
            }
            delete bmp;

            if (!saved) {
                MessageBoxW(hwnd, L"画像の保存に失敗しました", L"エラー", MB_OK | MB_ICONERROR);
                break;
            }

            // ダイアログ表示 (画像コピー / フォルダを開く / キャンセル)
            std::wstring dirStr = dir.wstring();
            int result = ShowEquipSaveDialog(hwnd, pngPath, dirStr);
            if (result == 1) {
                // PNGファイルをクリップボードにコピー (CF_HDROP)
                CopyFileToClipboard(hwnd, pngPath);
            } else if (result == 2) {
                // フォルダを開く
                ShellExecuteW(hwnd, L"open", dirStr.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            break;
        }
        }
        return 0;
    }

    case WM_NOTIFY:
    {
        NMHDR* nm = (NMHDR*)lParam;

        // タブ切り替え
        if (nm->hwndFrom == g_hNotebook && nm->code == TCN_SELCHANGE) {
            OnTabSelChanged();
            return 0;
        }

        // ListView ヘッダーの列リサイズ完了時に再描画を強制
        if (nm->code == HDN_ENDTRACKW || nm->code == HDN_DIVIDERDBLCLICKW) {
            if (g_hItemList && nm->hwndFrom == ListView_GetHeader(g_hItemList)) {
                InvalidateRect(g_hItemList, nullptr, TRUE);
                return 0;
            }
            if (g_hRateRotenList && nm->hwndFrom == ListView_GetHeader(g_hRateRotenList)) {
                InvalidateRect(g_hRateRotenList, nullptr, TRUE);
                return 0;
            }
            if (g_hDropList && nm->hwndFrom == ListView_GetHeader(g_hDropList)) {
                InvalidateRect(g_hDropList, nullptr, TRUE);
                return 0;
            }
            if (g_hEquipList && nm->hwndFrom == ListView_GetHeader(g_hEquipList)) {
                InvalidateRect(g_hEquipList, nullptr, TRUE);
                return 0;
            }
        }


        // RichEdit リンククリック → 既定ブラウザで開く
        {
            bool isChatEdit = false;
            for (int i = 0; i < IDC_CHAT_FILTER_COUNT; i++) {
                if (nm->hwndFrom == g_hChatEdits[i]) { isChatEdit = true; break; }
            }
            if (isChatEdit && nm->code == EN_LINK) {
                ENLINK* enl = reinterpret_cast<ENLINK*>(lParam);
                if (enl->msg == WM_LBUTTONUP) {
                    TEXTRANGEW tr{};
                    tr.chrg = enl->chrg;
                    int len = enl->chrg.cpMax - enl->chrg.cpMin;
                    if (len > 0 && len < 2048) {
                        std::vector<wchar_t> buf(len + 1, L'\0');
                        tr.lpstrText = buf.data();
                        SendMessageW(nm->hwndFrom, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tr));
                        ShellExecuteW(hwnd, L"open", buf.data(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
                return 0;
            }
        }
        // タブ オーナードロー
        if (nm->hwndFrom == g_hNotebook && nm->code == NM_CUSTOMDRAW) {
            // owner-draw は WM_DRAWITEM で処理
        }

        // ListView 仮想データ
        if (nm->hwndFrom == g_hItemList && nm->code == LVN_GETDISPINFOW) {
            NMLVDISPINFOW* di = (NMLVDISPINFOW*)lParam;
            int row = di->item.iItem;
            int col = di->item.iSubItem;

            std::lock_guard<std::mutex> lk(g_items_mutex);
            if (row < 0 || row >= (int)g_items.size()) return 0;
            auto& ie = g_items[row];

            std::wstring* src = nullptr;
            switch (col) {
            case 0: src = &ie.time; break;
            case 1: src = &ie.name; break;
            case 2: src = &ie.op1;  break;
            case 3: src = &ie.op2;  break;
            case 4: src = &ie.op3;  break;
            }
            if (src && (di->item.mask & LVIF_TEXT)) {
                wcsncpy_s(di->item.pszText, di->item.cchTextMax, src->c_str(), _TRUNCATE);
            }
            return 0;
        }

        // ListView カスタムドロー（通知対象アイテムの色付け・セル点滅）
        if (nm->hwndFrom == g_hItemList && nm->code == NM_CUSTOMDRAW) {
            NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lParam;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
                return CDRF_NOTIFYSUBITEMDRAW;
            case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
            {
                int row = (int)cd->nmcd.dwItemSpec;
                int col = cd->iSubItem;
                std::lock_guard<std::mutex> lk(g_items_mutex);
                if (row < 0 || row >= (int)g_items.size()) return CDRF_DODEFAULT;
                auto& ie = g_items[row];

                // デフォルト背景
                cd->clrTextBk = CLR_DEFAULT;

                if (ie.isNotificationTarget) {
                    cd->clrText = RGB(0, 100, 180);

                    // 点滅中のセルをチェック
                    for (auto& bc : g_blinkCells) {
                        if (bc.row == row && bc.col == col) {
                            if (bc.isOn) {
                                cd->clrTextBk = RGB(255, 230, 180); // 点灯色
                            }
                            break;
                        }
                    }

                    // 点滅終了後の恒久ハイライト
                    if (cd->clrTextBk == CLR_DEFAULT && col >= 1 && col <= 4 && ie.cellMatch[col]) {
                        // 点滅が終わった後だけ恐久色を適用
                        bool stillBlinking = false;
                        for (auto& bc : g_blinkCells) {
                            if (bc.row == row && bc.col == col) { stillBlinking = true; break; }
                        }
                        if (!stillBlinking) {
                            cd->clrTextBk = RGB(220, 230, 200); // 恐久ハイライト
                        }
                    }
                }
                return CDRF_NEWFONT;
            }
            }
            return CDRF_DODEFAULT;
        }

        // ドロップ ListView 仮想データ
        if (nm->hwndFrom == g_hDropList && nm->code == LVN_GETDISPINFOW) {
            NMLVDISPINFOW* di = (NMLVDISPINFOW*)lParam;
            int row = di->item.iItem;
            int col = di->item.iSubItem;

            std::wstring ts;
            std::wstring text;
            std::lock_guard<std::mutex> lk(g_drops_mutex);
            auto indices = GetFilteredDropIndices();
            if (row < 0 || row >= (int)indices.size()) return 0;
            int realIdx = indices[row];

            if (realIdx < 0 || realIdx >= (int)g_drops.size()) return 0;
            const auto& de = g_drops[realIdx];

            if (di->item.mask & LVIF_TEXT) {
                if (col == 0) {
                    ts = FormatTimestamp(de.timestamp);
                    wcsncpy_s(di->item.pszText, di->item.cchTextMax, ts.c_str(), _TRUNCATE);
                } else {
                    if (col == 1) text = de.itemName;
                    else if (col == 2) text = de.coordText;
                    if (col == 1 || col == 2) {
                        wcsncpy_s(di->item.pszText, di->item.cchTextMax, text.c_str(), _TRUNCATE);
                    }
                }
            }
            return 0;
        }

        // ドロップ ListView カスタムドロー（通知対象の色付け・行点滅）
        if (nm->hwndFrom == g_hDropList && nm->code == NM_CUSTOMDRAW) {
            NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lParam;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
            {
                int displayRow = (int)cd->nmcd.dwItemSpec;
                bool isNotificationTarget = false;
                bool isBlinkOn = false;
                bool isHighlight = false;

                std::lock_guard<std::mutex> lk(g_drops_mutex);
                auto indices = GetFilteredDropIndices();
                if (displayRow < 0 || displayRow >= (int)indices.size()) return CDRF_DODEFAULT;
                int realIdx = indices[displayRow];

                if (realIdx < 0 || realIdx >= (int)g_drops.size()) return CDRF_DODEFAULT;
                const auto& de = g_drops[realIdx];
                isNotificationTarget = de.isNotificationTarget;
                isHighlight = de.notifyHighlight;

                if (isNotificationTarget) {
                    cd->clrText = RGB(0, 100, 180);
                    // 点滅中
                    for (auto& br : g_dropBlinkRows) {
                        if (br.row == realIdx && br.isOn) {
                            isBlinkOn = true;
                            break;
                        }
                    }
                    // 恒久ハイライト
                    if (isBlinkOn) cd->clrTextBk = RGB(255, 230, 180);
                    else if (isHighlight) cd->clrTextBk = RGB(220, 230, 200);
                }
                return CDRF_NEWFONT;
            }
            }
            return CDRF_DODEFAULT;
        }

        // 装備 ListView 仮想データ
        if (nm->hwndFrom == g_hEquipList && nm->code == LVN_GETDISPINFOW) {
            NMLVDISPINFOW* di = (NMLVDISPINFOW*)lParam;
            int row = di->item.iItem;
            int col = di->item.iSubItem;
            if (row < 0 || row >= (int)g_equips.size()) return 0;
            auto& ee = g_equips[row];
            if (di->item.mask & LVIF_TEXT) {
                const std::wstring* src = nullptr;
                switch (col) {
                case 0: src = &ee.slotName; break;
                case 1: src = &ee.itemName; break;
                case 2: src = &ee.opName[0]; break;
                case 3: src = &ee.opName[1]; break;
                case 4: src = &ee.opName[2]; break;
                }
                if (src) wcsncpy_s(di->item.pszText, di->item.cchTextMax, src->c_str(), _TRUNCATE);
            }
            return 0;
        }

        // G員 ListView 仮想データ
        if (nm->hwndFrom == g_hGuildList && nm->code == LVN_GETDISPINFOW) {
            NMLVDISPINFOW* di = (NMLVDISPINFOW*)lParam;
            int row = di->item.iItem;
            int col = di->item.iSubItem;
            if (row < 0 || row >= (int)g_guildMembers.size()) return 0;
            auto& ge = g_guildMembers[row];
            if (di->item.mask & LVIF_TEXT) {
                const std::wstring* src = nullptr;
                switch (col) {
                case 0: src = &ge.name; break;
                case 1: src = &ge.level; break;
                case 2: src = &ge.job; break;
                case 3: src = &ge.rank; break;
                }
                if (src) wcsncpy_s(di->item.pszText, di->item.cchTextMax, src->c_str(), _TRUNCATE);
            }
            return 0;
        }

        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;

        // チャットフィルタボタン オーナードロー
        if (dis->CtlType == ODT_BUTTON &&
            dis->CtlID >= IDC_CHAT_FILTER_ALL && dis->CtlID <= IDC_CHAT_FILTER_WHISPER)
        {
            int idx = dis->CtlID - IDC_CHAT_FILTER_ALL;
            bool isOn = (g_chatFilterActive == idx);
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;

            // 親背景クリア
            HBRUSH hParentBg = (HBRUSH)(COLOR_BTNFACE + 1);
            FillRect(hdc, &rc, hParentBg);

            Gdiplus::Graphics graphics(hdc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

            const float inset = 0.5f;
            Gdiplus::RectF rectf(
                (Gdiplus::REAL)(rc.left + inset), (Gdiplus::REAL)(rc.top + inset),
                (Gdiplus::REAL)(rc.right - rc.left - inset * 2),
                (Gdiplus::REAL)(rc.bottom - rc.top - inset * 2));
            float radius = 4.0f;  // 控えめな角丸

            Gdiplus::GraphicsPath path;
            path.AddArc(rectf.X, rectf.Y, radius * 2, radius * 2, 180, 90);
            path.AddArc(rectf.X + rectf.Width - radius * 2, rectf.Y, radius * 2, radius * 2, 270, 90);
            path.AddArc(rectf.X + rectf.Width - radius * 2, rectf.Y + rectf.Height - radius * 2, radius * 2, radius * 2, 0, 90);
            path.AddArc(rectf.X, rectf.Y + rectf.Height - radius * 2, radius * 2, radius * 2, 90, 90);
            path.CloseFigure();

            // ON=グレー背景（押下中）, OFF=白背景（通常）
            COLORREF bg = isOn ? RGB(200, 200, 200) : RGB(255, 255, 255);
            Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
            graphics.FillPath(&brush, &path);

            // 枠線
            COLORREF border = isOn ? RGB(130, 130, 130) : RGB(190, 190, 190);
            Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(border), GetGValue(border), GetBValue(border)), 1.0f);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            pen.SetAlignment(Gdiplus::PenAlignmentInset);
            graphics.DrawPath(&pen, &path);

            // テキスト
            wchar_t text[64] = {};
            GetWindowTextW(dis->hwndItem, text, 64);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, isOn ? RGB(40, 40, 40) : RGB(150, 150, 150));

            HFONT oldFont = nullptr;
            if (g_chatFilterFont) oldFont = (HFONT)SelectObject(hdc, g_chatFilterFont);
            DrawTextW(hdc, text, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            if (oldFont) SelectObject(hdc, oldFont);

            return TRUE;
        }

        // タブオーナードロー
        if (dis->CtlType == ODT_TAB && dis->hwndItem == g_hNotebook) {
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            int idx = (int)dis->itemID;
            int sel = TabCtrl_GetCurSel(g_hNotebook);
            bool isSelected = (sel == idx);

            HBRUSH brush = CreateSolidBrush(isSelected ? RGB(255, 248, 250) : RGB(240, 225, 235));
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);

            // ★フラグに基づいてタブテキストを決定（TabCtrl_SetItemを使わず自前描画）
            std::wstring tabText;
            if (idx >= 0 && idx < (int)g_visibleTabs.size()) {
                tabText = g_visibleTabs[idx].name;
                if (tabText == L"チャット" && g_chatTabHasUnread)
                    tabText = L"★チャット";
            }

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, isSelected ? RGB(60, 60, 60) : RGB(110, 110, 110));
            HFONT oldf = nullptr;
            if (g_uiFont) oldf = (HFONT)SelectObject(hdc, g_uiFont);

            DrawTextW(hdc, tabText.c_str(), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            if (oldf) SelectObject(hdc, oldf);
            return TRUE;
        }

        // ツールバーボタン オーナードロー
        // 設定ボタン：枠なしで画像のみ描画（C# FlatStyle.Flat + BorderSize=0 相当）
        if (dis->CtlID == IDC_BTN_SETTINGS) {
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            // 親の背景色で塗りつぶし
            HBRUSH bgBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);
            if (g_settingsBitmap) {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                int imgSize = 20;
                int ix = rc.left + (rc.right - rc.left - imgSize) / 2;
                int iy = rc.top + (rc.bottom - rc.top - imgSize) / 2;
                graphics.DrawImage(g_settingsBitmap, ix, iy, imgSize, imgSize);
            } else {
                // フォールバック: テキスト
                if (g_uiFont) SelectObject(hdc, g_uiFont);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(30, 30, 30));
                DrawTextW(hdc, L"\u2699", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
            return TRUE;
        }

        // ヘルプボタン：help.png アイコン描画
        if (dis->CtlID == IDC_GUILD_HELP_BTN) {
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;
            HBRUSH bgBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);
            if (g_helpBitmap) {
                Gdiplus::Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                int imgSize = 20;
                int ix = rc.left + (rc.right - rc.left - imgSize) / 2;
                int iy = rc.top + (rc.bottom - rc.top - imgSize) / 2;
                graphics.DrawImage(g_helpBitmap, ix, iy, imgSize, imgSize);
            } else {
                if (g_uiFont) SelectObject(hdc, g_uiFont);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(30, 30, 30));
                DrawTextW(hdc, L"?", -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            }
            return TRUE;
        }

        bool isToolbarBtn = (dis->CtlID == IDC_BTN_STOP || dis->CtlID == IDC_BTN_START ||
            dis->CtlID == IDC_BTN_DC || dis->CtlID == IDC_BTN_SCREEN ||
            dis->CtlID == IDC_BTN_MAXV);

        if (isToolbarBtn) {
            HDC hdc = dis->hDC;
            RECT rc = dis->rcItem;

            Gdiplus::Graphics graphics(hdc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

            const float inset = 0.75f;
            Gdiplus::RectF rectf(
                (Gdiplus::REAL)(rc.left + inset), (Gdiplus::REAL)(rc.top + inset),
                (Gdiplus::REAL)(rc.right - rc.left - inset * 2),
                (Gdiplus::REAL)(rc.bottom - rc.top - inset * 2));
            float radius = 6.0f;

            Gdiplus::GraphicsPath path;
            path.AddArc(rectf.X, rectf.Y, radius * 2, radius * 2, 180, 90);
            path.AddArc(rectf.X + rectf.Width - radius * 2, rectf.Y, radius * 2, radius * 2, 270, 90);
            path.AddArc(rectf.X + rectf.Width - radius * 2, rectf.Y + rectf.Height - radius * 2, radius * 2, radius * 2, 0, 90);
            path.AddArc(rectf.X, rectf.Y + rectf.Height - radius * 2, radius * 2, radius * 2, 90, 90);
            path.CloseFigure();

            bool isDisabled = !IsWindowEnabled(dis->hwndItem);
            BYTE base = isDisabled ? 235 : ((dis->itemState & ODS_SELECTED) ? 230 : 245);
            Gdiplus::SolidBrush brushG(Gdiplus::Color(255, base, base, base));
            graphics.FillPath(&brushG, &path);

            BYTE penShade = isDisabled ? 210 : 200;
            Gdiplus::Pen penG(Gdiplus::Color(255, penShade, penShade, penShade), 1.0f);
            penG.SetLineJoin(Gdiplus::LineJoinRound);
            penG.SetAlignment(Gdiplus::PenAlignmentInset);
            graphics.DrawPath(&penG, &path);

            {
                HFONT oldFont = nullptr;
                if (g_uiFont) oldFont = (HFONT)SelectObject(hdc, g_uiFont);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, isDisabled ? RGB(120, 120, 120) : RGB(30, 30, 30));

                wchar_t textbuf[128];
                GetWindowTextW(dis->hwndItem, textbuf, _countof(textbuf));
                SIZE ts{};
                GetTextExtentPoint32W(hdc, textbuf, lstrlenW(textbuf), &ts);
                int topOff = (rc.bottom - rc.top - ts.cy) / 2;

                RECT textRc = rc;
                textRc.left += 6; textRc.right -= 6;
                textRc.top = rc.top + topOff;
                textRc.bottom = textRc.top + ts.cy;
                DrawTextW(hdc, textbuf, -1, &textRc, DT_SINGLELINE | DT_CENTER);
                if (oldFont) SelectObject(hdc, oldFont);
            }
            return TRUE;
        }

        // その他のオーナードローボタン（統一デザイン）
        if (dis->CtlType == ODT_BUTTON) {
            ThemeManager::DrawStyledButton(dis);
            return TRUE;
        }
        break;
    }

    case WM_TIMER:
    {
        if (wParam == TIMER_BLINK) {
            // 未読点滅（将来用）
        }
        else if (wParam == TIMER_CLEANUP) {
            // 定期クリーンアップ
            try {
                if (g_itemProcessor) g_itemProcessor->ClearCache();
            } catch (...) {}
        }
        else if (wParam == TIMER_EVENTLOG_BLINK) {
            if (g_eventLogBlinkCount <= 0) {
                KillTimer(hwnd, TIMER_EVENTLOG_BLINK);
                if (g_hItemEventLog) {
                    SetWindowTextW(g_hItemEventLog, L"");
                    g_eventLogBlinkOn = false;
                    InvalidateRect(g_hItemEventLog, nullptr, TRUE);
                }
                return 0;
            }
            g_eventLogBlinkOn = !g_eventLogBlinkOn;
            g_eventLogBlinkCount--;
            if (g_hItemEventLog) InvalidateRect(g_hItemEventLog, nullptr, TRUE);
        }
        else if (wParam == TIMER_EVENTLOG_CLEAR) {
            KillTimer(hwnd, TIMER_EVENTLOG_CLEAR);
            if (g_hItemEventLog) {
                // 同じメッセージがまだ表示中ならクリア
                wchar_t buf[512]{};
                GetWindowTextW(g_hItemEventLog, buf, 512);
                if (g_eventLogLastMessage == buf) {
                    SetWindowTextW(g_hItemEventLog, L"");
                }
            }
        }
        else if (wParam == TIMER_CELL_BLINK) {
            if (g_blinkCells.empty()) {
                KillTimer(hwnd, TIMER_CELL_BLINK);
                return 0;
            }
            for (auto it = g_blinkCells.begin(); it != g_blinkCells.end(); ) {
                it->isOn = !it->isOn;
                it->remaining--;
                if (it->remaining <= 0) {
                    it = g_blinkCells.erase(it);
                } else {
                    ++it;
                }
            }
            if (g_hItemList) InvalidateRect(g_hItemList, nullptr, FALSE);
            if (g_blinkCells.empty()) {
                KillTimer(hwnd, TIMER_CELL_BLINK);
            }
        }
        else if (wParam == BellProcessor::BELL_TIMER_ID) {
            if (g_bellProcessor) g_bellProcessor->OnTimerTick();
        }
        else if (wParam == TIMER_DROP_BLINK) {
            bool emptyAfterUpdate = false;
            {
                std::lock_guard<std::mutex> lk(g_drops_mutex);
                if (g_dropBlinkRows.empty()) {
                    KillTimer(hwnd, TIMER_DROP_BLINK);
                    return 0;
                }
                for (auto it = g_dropBlinkRows.begin(); it != g_dropBlinkRows.end(); ) {
                    it->isOn = !it->isOn;
                    it->remaining--;
                    if (it->remaining <= 0) {
                        // 恒久ハイライトに切り替え
                        if (it->row >= 0 && it->row < (int)g_drops.size()) {
                            g_drops[it->row].notifyBlink = false;
                            g_drops[it->row].notifyHighlight = true;
                        }
                        it = g_dropBlinkRows.erase(it);
                    } else {
                        ++it;
                    }
                }
                emptyAfterUpdate = g_dropBlinkRows.empty();
            }

            if (g_hDropList) InvalidateRect(g_hDropList, nullptr, FALSE);
            if (emptyAfterUpdate) {
                KillTimer(hwnd, TIMER_DROP_BLINK);
            }
        }
        return 0;
    }

    // ───── WM_APP メッセージハンドラ ─────

    case WM_CHAT_MSG:
    {
        auto* chatMsg = (ChatMessage*)lParam;
        if (chatMsg) {
            // ログ保存
            if (g_chatLogger) g_chatLogger->Log(*chatMsg);
            // UI 表示
            AddChatLogToUI(*chatMsg);
            delete chatMsg;
        }
        return 0;
    }

    case WM_ITEM_PICKUP:
    {
        auto* info = (ItemPickupInfo*)lParam;
        if (info) {
            // ログ保存
            if (g_itemLogger) g_itemLogger->LogItem(*info);

            // ListView に追加
            if (g_showAllItems || info->isNotificationTarget) {
                std::wstring ts = FormatTimestamp(info->timestamp);
                ItemEntry ie;
                ie.time = ts;
                ie.name = info->itemName;
                ie.op1 = (info->options.size() > 0) ? info->options[0] : L"";
                ie.op2 = (info->options.size() > 1) ? info->options[1] : L"";
                ie.op3 = (info->options.size() > 2) ? info->options[2] : L"";
                ie.isNotificationTarget = info->isNotificationTarget;

                // セル別マッチ情報を取得
                if (info->isNotificationTarget && g_wantedRegex) {
                    auto mr = g_wantedRegex->GetMatchingCells(info->itemName, info->options);
                    ie.cellMatch[1] = mr.itemMatch;  // アイテム名
                    for (size_t i = 0; i < mr.optionMatches.size() && i < 3; i++) {
                        ie.cellMatch[2 + i] = mr.optionMatches[i]; // op1,op2,op3
                    }
                }

                AddItemEntry(ie);

                // 点滅登録
                if (info->isNotificationTarget) {
                    int rowIdx;
                    {
                        std::lock_guard<std::mutex> lk(g_items_mutex);
                        rowIdx = (int)g_items.size() - 1;
                    }
                    for (int c = 1; c <= 4; c++) {
                        if (ie.cellMatch[c]) {
                            BlinkCell bc;
                            bc.row = rowIdx;
                            bc.col = c;
                            bc.remaining = 8;
                            bc.isOn = false;
                            g_blinkCells.push_back(bc);
                        }
                    }
                    if (!g_blinkCells.empty()) {
                        SetTimer(g_hwndMain, TIMER_CELL_BLINK, 250, nullptr);
                    }
                }
            }
            delete info;
        }
        return 0;
    }

    case WM_INV_FULL:
    {
        DisplayItemEventLog(L"【警告】インベントリ満杯", true);
        return 0;
    }

    case WM_DROP_ITEM:
    {
        auto* info = (DropItemInfo*)lParam;
        if (info) {
            DropEntry de;
            de.timestamp = info->timestamp;
            de.itemName = info->itemName;
            de.coordText = info->coordText;
            de.isNotificationTarget = info->isNotificationTarget;
            de.notifyBlink = info->isNotificationTarget;
            de.notifyHighlight = false;

            bool visible = g_showAllDrops || info->isNotificationTarget;
            {
                std::lock_guard<std::mutex> lk(g_drops_mutex);
                g_drops.push_back(de);
            }
            if (visible) {
                RefreshDropListView();
            }

            // ステータスバーにドロップ最新行を表示（ListViewと同じ表示条件に従う）
            if (visible && g_hLatestLabel) {
                std::wstring dropStatus = L"[" + FormatTimestamp(de.timestamp) + L"] ▼ " + de.itemName + L",  " + de.coordText;
                RECT rc;
                GetWindowRect(g_hLatestLabel, &rc);
                MapWindowPoints(HWND_DESKTOP, GetParent(g_hLatestLabel), (POINT*)&rc, 2);
                InvalidateRect(GetParent(g_hLatestLabel), &rc, TRUE);
                SetWindowTextW(g_hLatestLabel, dropStatus.c_str());
            }

            // 通知対象なら点滅・音
            if (info->isNotificationTarget) {
                {
                    std::lock_guard<std::mutex> lk(g_drops_mutex);
                    DropBlinkRow br;
                    br.row = (int)g_drops.size() - 1;
                    br.remaining = 12;
                    br.isOn = false;
                    g_dropBlinkRows.push_back(br);
                }
                SetTimer(g_hwndMain, TIMER_DROP_BLINK, 250, nullptr);

                if (g_soundService) g_soundService->PlayDropSound();
            }
            delete info;
        }
        return 0;
    }

    case WM_EQUIP_DATA:
    {
        auto* data = (EquipmentData*)lParam;
        if (data) {
            g_equips.clear();
            g_equips.resize(data->slots.size());
            for (size_t i = 0; i < data->slots.size(); i++) {
                auto& s = data->slots[i];
                auto& e = g_equips[i];
                e.slotName = s.slotName;
                e.itemName = s.isEmpty ? std::wstring() : s.itemName;
                for (int oi = 0; oi < 3; oi++) {
                    e.opName[oi] = s.opName[oi];
                    e.opVal[oi] = s.opValStr[oi];
                }
            }
            RefreshEquipListView();
            delete data;
        }
        return 0;
    }

    case WM_GUILD_MEMBER:
    {
        auto* result = (GuildMemberResult*)lParam;
        if (result) {
            // UI更新 (上書き)
            g_guildMembers.clear();
            g_guildName = result->guildName;
            for (auto& m : result->members) {
                GuildEntry ge;
                ge.name = m.name;
                ge.level = std::to_wstring(m.level);
                ge.job = m.jobName;
                ge.rank = m.rankName;
                g_guildMembers.push_back(std::move(ge));
            }
            RefreshGuildListView();

            // 手動保存用に最終結果を保持
            g_lastGuildResult = *result;

            // CSV自動保存 (設定間隔制限付き)
            if (g_guildMemberProcessor) {
                int interval = g_settings ? g_settings->guildSaveIntervalHours : 72;
                g_guildMemberProcessor->SaveCsvIfNeeded(GetExeDir(), *result, interval);
            }

            delete result;
        }
        return 0;
    }

    case WM_BELL_LABEL:
    {
        wchar_t* buf = (wchar_t*)lParam;
        if (buf) {
            std::wstring s(buf);
            if (g_hBellLabel) SetWindowTextW(g_hBellLabel, s.c_str());
            bool isActive = !s.empty() && s.find(L"--") == std::wstring::npos && s.find(L"00:00") == std::wstring::npos;
            g_bellActive = isActive;
            if (g_hBellLabel) InvalidateRect(g_hBellLabel, nullptr, TRUE);
            // ツールバー再レイアウト
            RECT rcClient; GetClientRect(hwnd, &rcClient);
            LayoutToolbar(hwnd, rcClient.right);
            delete[] buf;
        }
        return 0;
    }

    case WM_BELL_CHAT:
    {
        auto* pair = (std::pair<std::wstring, std::wstring>*)lParam;
        if (pair) {
            ChatMessage cMsg;
            cMsg.timestamp = std::chrono::system_clock::now();
            cMsg.chatType = ChatType::General;
            cMsg.senderName = pair->first;
            cMsg.message = pair->second;
            AddChatLogToUI(cMsg);
            if (g_chatLogger) g_chatLogger->Log(cMsg);
            delete pair;
        }
        return 0;
    }

    case WM_CAPTURE_STATE:
    {
        bool running = (wParam != 0);
        if (g_hBtnStop)  EnableWindow(g_hBtnStop,  running ? TRUE : FALSE);
        if (g_hBtnStart) EnableWindow(g_hBtnStart, running ? FALSE : TRUE);
        InvalidateRect(g_hBtnStop, nullptr, TRUE);
        InvalidateRect(g_hBtnStart, nullptr, TRUE);
        return 0;
    }

    case WM_RATE_ROTEN:
    {
        auto* result = (RateRotenResult*)lParam;
        if (result && g_hRateRotenList) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t timeBuf[16];
            std::swprintf(timeBuf, 16, L"%02d:%02d", st.wHour, st.wMinute);

            SendMessage(g_hRateRotenList, WM_SETREDRAW, FALSE, 0);

            // 親行 (キャラ名)
            LVITEMW lvi{};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = ListView_GetItemCount(g_hRateRotenList);
            lvi.iSubItem = 0;
            lvi.pszText = timeBuf;
            int parentIdx = ListView_InsertItem(g_hRateRotenList, &lvi);
            ListView_SetItemText(g_hRateRotenList, parentIdx, 1, const_cast<wchar_t*>(result->charName.c_str()));

            // 子行 (各アイテム)
            for (auto& it : result->items) {
                wchar_t slotBuf[8];
                std::swprintf(slotBuf, 8, L"[%d]", it.slot);
                wchar_t countBuf[16];
                std::swprintf(countBuf, 16, L"%d", it.count);
                wchar_t priceBuf[32];
                // 桁区切り付き価格表示
                {
                    uint32_t p = it.price;
                    std::wstring ps = std::to_wstring(p);
                    int len = (int)ps.size();
                    std::wstring formatted;
                    for (int i = 0; i < len; i++) {
                        if (i > 0 && (len - i) % 3 == 0) formatted += L',';
                        formatted += ps[i];
                    }
                    wcscpy_s(priceBuf, 32, formatted.c_str());
                }

                std::wstring op1 = it.options.size() > 0 ? it.options[0] : L"";
                std::wstring op2 = it.options.size() > 1 ? it.options[1] : L"";
                std::wstring op3 = it.options.size() > 2 ? it.options[2] : L"";

                lvi.iItem = ListView_GetItemCount(g_hRateRotenList);
                lvi.pszText = slotBuf;
                int childIdx = ListView_InsertItem(g_hRateRotenList, &lvi);
                ListView_SetItemText(g_hRateRotenList, childIdx, 1, const_cast<wchar_t*>(it.itemName.c_str()));
                ListView_SetItemText(g_hRateRotenList, childIdx, 2, countBuf);
                ListView_SetItemText(g_hRateRotenList, childIdx, 3, const_cast<wchar_t*>(op1.c_str()));
                ListView_SetItemText(g_hRateRotenList, childIdx, 4, const_cast<wchar_t*>(op2.c_str()));
                ListView_SetItemText(g_hRateRotenList, childIdx, 5, const_cast<wchar_t*>(op3.c_str()));
                ListView_SetItemText(g_hRateRotenList, childIdx, 6, priceBuf);
                ListView_SetItemText(g_hRateRotenList, childIdx, 7, const_cast<wchar_t*>(it.currency.c_str()));
            }

            SendMessage(g_hRateRotenList, WM_SETREDRAW, TRUE, 0);
            // 最新行（最終アイテム行）が表示エリア最下部に来るようスクロール
            int lastIdx = ListView_GetItemCount(g_hRateRotenList) - 1;
            if (lastIdx >= 0)
                ListView_EnsureVisible(g_hRateRotenList, lastIdx, FALSE);
        }
        delete result;
        return 0;
    }

    case WM_ROTEN_LOG:
    {
        auto* entry = (RotenLogEntry*)lParam;
        if (entry && g_hRotenEdit) {
            // timeText + " " + message + "\r\n"
            std::wstring line;
            if (!entry->timeText.empty())
                line = entry->timeText + L" " + entry->message + L"\r\n";
            else {
                SYSTEMTIME st;
                GetLocalTime(&st);
                wchar_t tb[8];
                std::swprintf(tb, 8, L"%02d:%02d", st.wHour, st.wMinute);
                line = std::wstring(tb) + L" " + entry->message + L"\r\n";
            }
            int len = GetWindowTextLengthW(g_hRotenEdit);
            SendMessage(g_hRotenEdit, EM_SETSEL, len, len);
            SendMessage(g_hRotenEdit, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
        }
        delete entry;
        return 0;
    }

    case WM_ROTEN_VIEWER_REFRESH:
        if (g_rotenProcessor) g_rotenProcessor->RefreshViewerIfOpen();
        return 0;

    case WM_SYSTEM_LOG:
    {
        wchar_t* buf = (wchar_t*)lParam;
        if (buf) {
            AddSystemLogDirect(buf);
            delete[] buf;
        }
        return 0;
    }

    case WM_GOLD_UPDATE:
    {
        if (g_goldProcessor) {
            auto s = g_goldProcessor->GetStats();
            auto fmtGold = [](int64_t v) -> std::wstring {
                // カンマ区切り + " Gold"
                std::wstring num = std::to_wstring(v);
                int len = (int)num.size();
                std::wstring formatted;
                for (int i = 0; i < len; i++) {
                    if (i > 0 && (len - i) % 3 == 0) formatted += L',';
                    formatted += num[i];
                }
                return formatted + L" Gold";
            };
            auto fmtCount = [](int v, const wchar_t* unit) -> std::wstring {
                std::wstring num = std::to_wstring(v);
                int len = (int)num.size();
                std::wstring formatted;
                for (int i = 0; i < len; i++) {
                    if (i > 0 && (len - i) % 3 == 0) formatted += L',';
                    formatted += num[i];
                }
                return formatted + L" " + unit;
            };
            if (g_hGoldMobCountVal) SetWindowTextW(g_hGoldMobCountVal, fmtCount(s.mobKillCount, L"体").c_str());
            if (g_hGoldMobVal)      SetWindowTextW(g_hGoldMobVal, fmtGold(s.mobGoldTotal).c_str());
            if (g_hGoldPickupVal)   SetWindowTextW(g_hGoldPickupVal, fmtGold(s.pickupGoldTotal).c_str());
            if (g_hGoldMerchantVal) SetWindowTextW(g_hGoldMerchantVal, fmtGold(s.merchantGoldTotal).c_str());
            if (g_hGoldTotalVal)    SetWindowTextW(g_hGoldTotalVal, fmtGold(s.pickupGoldTotal + s.merchantGoldTotal + s.mobGoldTotal).c_str());
            if (g_hGoldDecompVal)   SetWindowTextW(g_hGoldDecompVal, fmtCount(s.decomposeCount, L"個").c_str());
            if (g_hGoldCrystalVal)  SetWindowTextW(g_hGoldCrystalVal, fmtCount(s.crystalStoneCount, L"個").c_str());
            if (g_hGoldUmuVal)      SetWindowTextW(g_hGoldUmuVal, fmtCount(s.umuCoinCount, L"個").c_str());
        }
        return 0;
    }

    case WM_PORTS_UPDATED:
    {
        auto* ports = (std::set<int>*)lParam;
        if (ports && g_captureService) {
            std::set<uint16_t> portSet;
            for (int p : *ports) portSet.insert((uint16_t)p);
            g_captureService->SetPortFilter(portSet);
            delete ports;
        }
        return 0;
    }

    case WM_AUTO_START:
    {
        ShowStartupReferenceLinks();
        StartCapture();
        return 0;
    }

    case WM_APP + 11:
    {
        // BellProcessor パケット（UIスレッドで処理）
        auto* payload = (std::vector<uint8_t>*)lParam;
        if (payload && g_bellProcessor) {
            try {
                g_bellProcessor->ProcessPacket(payload->data(), (int)payload->size(), (wParam != 0));
            } catch (...) {}
            delete payload;
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        // EventLogラベル/ステータスバーの色制御
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;

        if (hCtrl == g_hItemEventLog) {
            // 点滅中は赤/黒切り替え
            if (g_eventLogBlinkCount > 0) {
                SetTextColor(hdc, g_eventLogBlinkOn ? RGB(255, 0, 0) : RGB(0, 0, 0));
            } else {
                SetTextColor(hdc, RGB(100, 100, 100));
            }
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }

        if (hCtrl == g_hLatestLabel || hCtrl == g_hItemEventTitle) {
            SetTextColor(hdc, RGB(100, 100, 100));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    }

    case WM_DESTROY:
    {
        KillTimer(hwnd, TIMER_BLINK);
        KillTimer(hwnd, TIMER_CLEANUP);
        KillTimer(hwnd, TIMER_EVENTLOG_BLINK);
        KillTimer(hwnd, TIMER_EVENTLOG_CLEAR);
        KillTimer(hwnd, TIMER_CELL_BLINK);
        KillTimer(hwnd, TIMER_DROP_BLINK);
        KillTimer(hwnd, BellProcessor::BELL_TIMER_ID);

        // キャプチャ停止
        if (g_captureService) g_captureService->Stop();

        // サービス停止
        if (g_soundService)  g_soundService->Shutdown();
        if (g_processMonitor) g_processMonitor->StopMonitoring();

        // 設定保存
        if (g_settings) {
            g_settings->geometry = GetGeometry(hwnd);
            g_settings->selectedTabs = L"";
            for (size_t i = 0; i < g_selectedTabs.size(); i++) {
                if (i > 0) g_settings->selectedTabs += L",";
                g_settings->selectedTabs += g_selectedTabs[i];
            }
            g_settings->Save();
        }

        // リソース解放
        g_bellProcessor.reset();
        g_goldProcessor.reset();
        g_dropProcessor.reset();
        g_dcManager.reset();
        g_captureService.reset();
        g_soundService.reset();
        g_processMonitor.reset();
        g_chatLogger.reset();
        g_itemLogger.reset();
        g_itemProcessor.reset();
        g_chatProcessor.reset();
        g_wantedRegex.reset();
        g_itemDatLoader.reset();
        g_settings.reset();

        if (g_helpBitmap) { delete g_helpBitmap; g_helpBitmap = nullptr; }
        if (g_settingsBitmap) { delete g_settingsBitmap; g_settingsBitmap = nullptr; }
        if (g_uiFont) { DeleteObject(g_uiFont); g_uiFont = nullptr; }
        if (g_uiFontBold) { DeleteObject(g_uiFontBold); g_uiFontBold = nullptr; }
        if (g_logFont) { DeleteObject(g_logFont); g_logFont = nullptr; }
        if (g_chatFilterFont) { DeleteObject(g_chatFilterFont); g_chatFilterFont = nullptr; }
        if (g_gdiplusToken) { Gdiplus::GdiplusShutdown(g_gdiplusToken); g_gdiplusToken = 0; }

        PostQuitMessage(0);
        return 0;
    }

    } // switch
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ───────────────────────── wWinMain ─────────────────────────
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    // Common Controls 初期化
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    // ClientSize 600x400
    RECT r = {0, 0, 600, 400};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int winw = r.right - r.left;
    int winh = r.bottom - r.top;

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"JRS Chat Logger (C++)",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, winw, winh,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
