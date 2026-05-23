// dc_setting_dialog.cpp — DCSettingDialog: ドロップクリーン設定ダイアログ
// C# DCSettingDialog.cs の C++ 移植 (Win32ダイアログ)
#include "dc_setting_dialog.h"
#include "dc_manager.h"
#include "app_settings.h"
#include "item_dat_loader.h"
#include "theme_manager.h"
#include "resource.h"

#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ── テキスト入力ダイアログ用データ ──
struct InputDlgData { const wchar_t* msg; const wchar_t* cap; std::wstring result; };

static INT_PTR CALLBACK InputDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    static HWND hLabel, hEdit, hOk, hCancel;
    if (msg == WM_INITDIALOG) {
        { HICON hIco = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON)); if (hIco) { SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIco); SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco); } }
        auto* data = (InputDlgData*)lp;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)data);
        SetWindowTextW(hDlg, data->cap);
        HFONT hFont = ThemeManager::CreateDefaultFont();

        hLabel = CreateWindowExW(0, L"STATIC", data->msg,
            WS_CHILD | WS_VISIBLE, 8, 6, 260, 36, hDlg, nullptr, nullptr, nullptr);
        hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 8, 46, 260, 22, hDlg, (HMENU)1001, nullptr, nullptr);
        hOk = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 112, 76, 76, 24, hDlg, (HMENU)IDOK, nullptr, nullptr);
        hCancel = CreateWindowExW(0, L"BUTTON", L"キャンセル",
            WS_CHILD | WS_VISIBLE, 192, 76, 76, 24, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);

        for (HWND h : {hLabel, hEdit, hOk, hCancel})
            SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        CenterWindowOnParent(hDlg, GetParent(hDlg));
        SetFocus(hEdit);
        return FALSE;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == IDOK) {
            auto* data = (InputDlgData*)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
            wchar_t buf[256] = {};
            GetWindowTextW(hEdit, buf, 256);
            data->result = buf;
            while (!data->result.empty() && (data->result.front() == L' ' || data->result.front() == L'\t'))
                data->result.erase(data->result.begin());
            while (!data->result.empty() && (data->result.back() == L' ' || data->result.back() == L'\t'))
                data->result.pop_back();
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
    }
    if (msg == WM_CLOSE) { EndDialog(hDlg, IDCANCEL); return TRUE; }
    return FALSE;
}

static std::wstring ShowInputDialog(HWND hParent, const wchar_t* message, const wchar_t* caption) {
    InputDlgData idd = { message, caption, L"" };

    WORD dlgBuf[512] = {};
    DLGTEMPLATE* pDlg = (DLGTEMPLATE*)dlgBuf;
    pDlg->style = DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    pDlg->dwExtendedStyle = 0;
    pDlg->cdit = 0;
    pDlg->cx = 190; pDlg->cy = 72;
    WORD* p = (WORD*)(pDlg + 1);
    *p++ = 0; *p++ = 0; *p++ = 0;

    INT_PTR ret = DialogBoxIndirectParamW(GetModuleHandleW(nullptr),
        pDlg, hParent, InputDlgProc, (LPARAM)&idd);
    return (ret == IDOK) ? idd.result : L"";
}

// ── 内部データ構造 ──
struct DlgData {
    DCManager* manager = nullptr;
    AppSettings* settings = nullptr;
    ItemDatLoader* itemDatLoader = nullptr;

    HWND hDlg = nullptr;
    HWND hComboProfile = nullptr;
    HWND hComboCategory = nullptr;
    HWND hEditSearch = nullptr;
    HWND hListItems = nullptr;
    HWND hStatus = nullptr;
    HWND hSuggestions = nullptr;     // 検索候補ListBox
    HWND hToolTip = nullptr;          // ツールチップ
    std::vector<int> suggestionIndices; // 候補のblockItemsインデックス

    // アイテムデータ
    struct BlockItem {
        int id = 0;
        int type = -1;
        std::wstring name;
        int basePrice = 0;
    };
    std::vector<BlockItem> blockItems;
    std::vector<uint8_t> dcBytes;

    // 表示中のインデックス
    std::vector<int> displayIndices;

    // カテゴリ情報
    struct CatEntry {
        std::wstring name;
        bool isHeader = false;
        int typeId = -1;
    };
    std::vector<CatEntry> categories;

    bool suppressCheck = false;
    bool isLoading = false;
    std::wstring sp2TipText;  // 特殊2ツールチップ用バッファ
};

// ── 特殊リスト ──
static const std::unordered_map<std::wstring, std::vector<int>>& GetSpecialLists() {
    static std::unordered_map<std::wstring, std::vector<int>> lists;
    if (lists.empty()) {
        lists[L"ノーマルU"] = {2552,280,281,282,283,284,285,286,287,288,289,290,291,292,293,294,295,296,297,298,299,300,301,302,303,304,305,306,307,308,309,310,311,312,313,314,315,316,317,318,319,320,321,322,323,324,325,326,327,328,329,330,331,332,333,334,335,336,337,338,339,340,341,342,343,344,345,346,347,348,349,350,351,352,353,354,355,356,357,358,359,360,361,362,363,364,365,366,367,368,369,370,371,372,373,374,375,376,377,378,379,380,381,382,383,384,385,386,387,388,389,390,391,392,393,394,395,396,397,398,399,400,401,402,403,404,405,406,407,408,409,410,411,412,413,414,415,416,417,418,419,420,421,422,423,424,425,426,427,428,429,430,431,432,433,434,435,436,437,438,439,440,441,442,443,444,445,446,447,448,449,450,451,452,453,454,455,456,457,458,459,460,461,462,463,464,465,466,467,468,469,470,471,472,473,474,475,476,477,478,479,480,481,482,483,4344,4345,4346,4347,4348,4349,4365,4366,4367,4368,4369,4370,4371,4393,4628,4629,4630,4631,4632,4633,4634,4635,4636,4637,4638,4639,4640,4641,4645,4756,4757,4758,4759,4760,4761,4762,4763,4764,4765,4766,4767,4768,4769,4770,4771,4790};
        lists[L"DXU"] = {1389,1390,3019,3020,3021,3022,3023,3024,3025,3026,3027,3028,3029,3030,3031,3032,3033,3034,3035,3036,3037,3038,3039,3040,3041,3042,3043,3044,3045,3046,3047,3048,3049,3050,3051,3052,3053,3054,3055,3056,3057,3058,3059,3060,3061,3062,3063,3064,3065,3066,3067,3068,3069,3070,3071,3072,3073,3074,3075,3076,3077,3078,3079,3080,3081,3082,3083,3084,3085,3086,3087,3088,3089,3090,3091,3092,3093,3094,3095,3096,3097,3098,3099,3100,3101,3102,3103,3104,3105,3106,3107,3108,3109,3110,3111,3112,3113,3114,3115,3116,3117,3118,3119,3120,3121,3122,3123,3124,3125,3126,3127,3128,3129,3130,3131,3132,3133,3134,3135,3136,3137,3138,3139,3140,3141,3142,3143,3144,3145,3146,3147,3148,3149,3150,3151,3152,3153,3154,3155,3156,3157,3158,3159,3160,3161,3162,3163,3164,3165,3166,3167,3168,3169,3170,3171,3172,3173,3174,3175,3176,3177,3178,3179,3180,3181,3182,3183,3184,3185,3186,3187,3188,3189,3190,3191,3192,3193,3194,3195,3196,3197,3198,3199,3200,3201,3202,3203,3204,3205,3206,3207,3208,3209,3210,3211,3212,3213,3214,3215,3216,3217,3218,3219,3220,3221,3222,3223,3224,3225,3226,3227,3228,3229,3230,3231,3232,3233,3234,3235,3236,3237,3238,3239,3240,3241,3242,2446,2447,2448,2449,2450,2451,2452,2453,2454,2455,2456,2457,2458,2459,2460,2461,2462,2463,2464,2465,2466,2467,2468,2469,2470,2471,2472,2473,2474,2475,2476,2477,2478,2479,2480,2481,2482,2483,2484,2485,2486,2487,2488,2489,2490,2491,2492,2493,2494,2495,2496,2497,2498,2499,2500,2501,2502,2503,2504,2505,2506,2507,2508,2509,2510,2511,2512,2513,2514,2515,2516,2517,2518,2519,2520,2521,2522,2523,2524,2525,2526,2527,2528,2529,2530,2531,2532,2533,2534,2535,2536,2537,2538,2539,2540,2541,2542,2543,2544,2545,2546,2547,2548,2549,4372,4373,4374,4375,4376,4377,4378,4379,4380,4381,4382,4383,4384,4385,4386,4387,4394,4646,4647,4648,4649,4650,4651,4652,4653,4654,4655,4656,4657,4658,4659,4661,4662,4772,4773,4774,4775,4776,4777,4778,4779,4780,4781,4782,4783,4784,4785,4786,4791};
        lists[L"BFU"] = {4937,4938,4939,4940,4941,4942,4943,4944,4945,4946,4947,4948,4949,4950,4951,4952,4953};
        lists[L"800"] = {265,4902,4903,4905,4906,4907,4908,4909,4910,4911,4912,4913,4914,4915,4916,4917,4918,4919,4920,4921,4922,4923,4924,4925,4926,4927,4928,4929,4930,4931,4932,4933,4934,4935,4936,4980,4981,4982,4983,4984,4985,4986,4987,4988,4989,4990,4991,4992,4993,4994,4995,4996,4997,4998,4999,5000,5001,5002,5003,5004,5005,5006,5007,5009,5010,5011,5012,5013,5014};
        lists[L"900"] = {264,2949,2951,4867,4868,4869,4870,4871,4872,4873,4874,4875,4876,4877,4878,4879,4880,4881,4882,4883,4884,4885,4886,4887,4888,4889,4890,4891,4892,4893,4894,4895,4896,4897,4898,4899,4900,4901,4954,4955,4956,4957,4958,4959,4960,4961,4962,4963,4964,4965,4966,4967,4968,4969,4970,4971,4972,4973,4975,4976,4977,4978,4979,5070};
        lists[L"1000"] = {267,268,269,270,271,272,273,274,279,610,611,612,613,614,615,616,618,619,620,621,622,623,624,625,626,627,628,629,666,667,711,712,713,714,715,716,717,718,719,720,722,723,760,761,811,812,813,814,815,826,827,828,859,1263,1324,1739,1740,1741,1742,1743,2950,3676,4904};
        lists[L"2100"] = {1879,1880,1881,1882,1883,1884,1885,2586,2587,2588,2888,2889,2890,2891,2952,2953,2954,2955,2956,2957,2958,2959,2960,2961,2971,2972,2973,2974,2975,2976,2977,2978,2979,2980,3262,3263,3264,3268,3269,3270,3271,3272,3273,3274,3282,3307,3397,3424,3644,3645,3646,3647,3648,3649,3650,3651,3652,3653,3654,3655,3656,3657,3658,3659,3670,3671,3672,3673,3674,3675,3678,3681};
        lists[L"特殊"] = {617,1744,1869,1878,3669,3679,3680};
        lists[L"特殊2"] = {5015,5047,5044,5045,5046};
    }
    return lists;
}

// タイプ名マップ
static const std::unordered_map<int, std::wstring>& GetTypeNames() {
    static std::unordered_map<int, std::wstring> m = {
        {0,L"兜"},{1,L"冠"},{2,L"グローブ"},{3,L"槍投擲機"},{4,L"クロー"},{5,L"手首"},{6,L"ベルト"},{7,L"足"},
        {8,L"首"},{9,L"指"},{10,L"耳"},{11,L"背中"},{12,L"ブロ"},{13,L"腕刺青"},{14,L"肩刺青"},{15,L"十字架"},
        {16,L"鎧"},{17,L"職鎧"},{18,L"片手剣"},{19,L"盾"},{20,L"両手剣"},{21,L"杖"},{22,L"牙"},{23,L"鈍器"},
        {24,L"翼"},{25,L"投擲"},{26,L"弓"},{27,L"矢"},{28,L"槍"},{29,L"笛"},{30,L"スリング"},{31,L"ボトル"},
        {32,L"ステッキ"},{33,L"鞭"},{34,L"原石"},{35,L"赤POT"},{36,L"青POT"},{37,L"水薬"},{38,L"能力アップ"},{39,L"異常回復"},
        {40,L"復活系"},{41,L"鍵"},{42,L"帰還"},{43,L"必殺技の巻物"},{44,L"お菓子"},{45,L"霊薬"},{46,L"魔法液"},
        {47,L"セッティング原石"},{48,L"その他特殊アイテム"},{49,L"クエストアイテム"},{50,L"課金アイテム"},{51,L"エンチャント系"},{52,L"ロト系"},{54,L"鎌"},{55,L"闘士武器"},{56,L"本"}
    };
    return m;
}

static std::wstring GetTypeName(int type) {
    auto& m = GetTypeNames();
    auto it = m.find(type);
    if (it != m.end()) return it->second;
    return std::to_wstring(type);
}

static bool IsPlaceholder(const DlgData::BlockItem& item) {
    if (item.name.empty()) return true;
    if (item.name == L"valid name") return true;
    if (item.name.find(L"[D]") != std::wstring::npos) return true;
    if (item.name.find(L"[E]") != std::wstring::npos) return true;
    if (item.name.substr(0, 10) == L"インフィニティクロー") return true;
    if (item.name.size() >= 2 && item.name[0] == L'I' && item.name[1] == L'F') return true;
    if (item.name.size() >= 4 && item.name.substr(0, 4) == L"Rank") return true;
    return false;
}

// ゲーム側DC.datパスを取得
static std::wstring GetGameDcPath(AppSettings& settings) {
    if (!settings.redStoneDatPath.empty()) {
        auto maybe = (fs::path(settings.redStoneDatPath) / L"DC.dat").wstring();
        if (fs::exists(maybe) || fs::exists(fs::path(settings.redStoneDatPath))) return maybe;
    }
    auto reg = ItemDatLoader::GetRedStonePathFromRegistry();
    if (!reg.empty()) {
        auto maybe = (fs::path(reg) / L"DC.dat").wstring();
        if (fs::exists(maybe) || fs::exists(fs::path(reg))) return maybe;
    }
    wchar_t pf86[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, 0, pf86) == S_OK) {
        auto fallback = (fs::path(pf86) / L"GameON" / L"RED STONE" / L"DC.dat").wstring();
        auto dir = (fs::path(pf86) / L"GameON" / L"RED STONE").wstring();
        if (fs::exists(fallback) || fs::exists(dir)) return fallback;
    }
    return L"";
}

static void ExportToGame(DlgData& d) {
    auto gamePath = GetGameDcPath(*d.settings);
    if (!gamePath.empty()) {
        try {
            auto dir = fs::path(gamePath).parent_path();
            if (!fs::exists(dir)) fs::create_directories(dir);
            std::ofstream ofs(gamePath, std::ios::binary);
            if (ofs.is_open()) ofs.write(reinterpret_cast<const char*>(d.dcBytes.data()), d.dcBytes.size());
        } catch (...) {}
    }
}

static void SaveCurrentDc(DlgData& d) {
    if (d.isLoading) return;
    wchar_t buf[256];
    GetWindowTextW(d.hComboProfile, buf, 256);
    std::wstring profile(buf);
    if (profile.empty() || profile == L"新規作成...") return;
    d.manager->WriteDcBytes(profile, d.dcBytes);
    ExportToGame(d);
}

// フィルタリングと表示更新
static void RefreshFilter(DlgData& d) {
    d.displayIndices.clear();

    // 選択カテゴリ
    int catSel = (int)SendMessage(d.hComboCategory, CB_GETCURSEL, 0, 0);
    int filterType = -999;
    if (catSel >= 0 && catSel < (int)d.categories.size() && !d.categories[catSel].isHeader) {
        filterType = d.categories[catSel].typeId;
    }

    // 検索文字列
    wchar_t searchBuf[256] = {};
    GetWindowTextW(d.hEditSearch, searchBuf, 256);
    std::wstring q(searchBuf);

    for (int i = 0; i < (int)d.blockItems.size(); i++) {
        if (IsPlaceholder(d.blockItems[i])) continue;
        if (filterType != -999 && d.blockItems[i].type != filterType) continue;
        if (!q.empty()) {
            // case-insensitive search
            std::wstring lower = d.blockItems[i].name;
            std::wstring lq = q;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            std::transform(lq.begin(), lq.end(), lq.begin(), ::towlower);
            if (lower.find(lq) == std::wstring::npos) continue;
        }
        d.displayIndices.push_back(i);
    }
}

static void PopulateListView(DlgData& d) {
    d.suppressCheck = true;
    SendMessage(d.hListItems, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(d.hListItems);

    for (int i = 0; i < (int)d.displayIndices.size(); i++) {
        int realIdx = d.displayIndices[i];
        auto& item = d.blockItems[realIdx];

        LVITEMW lvi{};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = const_cast<wchar_t*>(item.name.c_str());
        lvi.lParam = (LPARAM)realIdx;
        int idx = ListView_InsertItem(d.hListItems, &lvi);

        bool checked = (realIdx < (int)d.dcBytes.size() && d.dcBytes[realIdx] == 1);
        ListView_SetCheckState(d.hListItems, idx, checked);
    }

    SendMessage(d.hListItems, WM_SETREDRAW, TRUE, 0);
    // アイテム追加後、垂直スクロールバー出現を考慮してカラム幅を再計算
    RECT lvrc; GetClientRect(d.hListItems, &lvrc);
    ListView_SetColumnWidth(d.hListItems, 0, lvrc.right);
    InvalidateRect(d.hListItems, nullptr, TRUE);
    d.suppressCheck = false;
}

// ── コントロールID ──
#define IDC_DC_COMBO_PROFILE  3001
#define IDC_DC_COMBO_CATEGORY 3002
#define IDC_DC_EDIT_SEARCH    3003
#define IDC_DC_LIST_ITEMS     3004
#define IDC_DC_BTN_CAT_ON     3010
#define IDC_DC_BTN_CAT_OFF    3011
#define IDC_DC_BTN_ALL_ON     3012
#define IDC_DC_BTN_ALL_OFF    3013
#define IDC_DC_LIST_SUGGEST   3050
// 特殊ボタン 3020-3029
#define IDC_DC_BTN_SPECIAL_BASE 3020
// クイックボタン 3030-3033
#define IDC_DC_BTN_QUICK_GOLD   3030
#define IDC_DC_BTN_QUICK_COIN   3031
#define IDC_DC_BTN_QUICK_CHIKA  3032
#define IDC_DC_BTN_QUICK_BADGE  3033

// ツールチップ設定ヘルパー
static void SetTip(HWND hToolTip, HWND hDlg, HWND hCtrl, const wchar_t* text) {
    TOOLINFOW ti{};
    ti.cbSize = TTTOOLINFOW_V1_SIZE;  // 互換性のためV1サイズ使用
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = hDlg;
    ti.uId = (UINT_PTR)hCtrl;
    ti.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(hToolTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

static void UpdateTip(HWND hToolTip, HWND hDlg, HWND hCtrl, const wchar_t* text) {
    TOOLINFOW ti{};
    ti.cbSize = TTTOOLINFOW_V1_SIZE;
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = hDlg;
    ti.uId = (UINT_PTR)hCtrl;
    ti.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(hToolTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

// アイテムIDで検索→カテゴリ切替+ListView選択 (C# SelectItemById 相当)
static void SelectItemById(DlgData& d, int id) {
    if (d.blockItems.empty()) {
        MessageBoxW(d.hDlg, L"アイテムデータが読み込まれていません。プロファイルを選択してください。", L"情報", MB_OK | MB_ICONINFORMATION);
        return;
    }
    int realIndex = -1;
    for (int i = 0; i < (int)d.blockItems.size(); i++) {
        if (d.blockItems[i].id == id) { realIndex = i; break; }
    }
    if (realIndex < 0 || IsPlaceholder(d.blockItems[realIndex])) {
        MessageBoxW(d.hDlg, L"該当アイテムが見つかりません。", L"情報", MB_OK | MB_ICONINFORMATION);
        return;
    }
    auto cat = GetTypeName(d.blockItems[realIndex].type);
    for (int i = 0; i < (int)d.categories.size(); i++) {
        if (!d.categories[i].isHeader && d.categories[i].name == cat) {
            SendMessage(d.hComboCategory, CB_SETCURSEL, i, 0);
            break;
        }
    }
    RefreshFilter(d);
    PopulateListView(d);
    for (int i = 0; i < (int)d.displayIndices.size(); i++) {
        if (d.displayIndices[i] == realIndex) {
            ListView_SetItemState(d.hListItems, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(d.hListItems, i, FALSE);
            SetFocus(d.hListItems);
            break;
        }
    }
}

// 候補ListBox選択→カテゴリ切替+ListView選択
static void OnSuggestionClicked(DlgData& d) {
    int sel = (int)SendMessage(d.hSuggestions, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)d.suggestionIndices.size()) return;
    int realIdx = d.suggestionIndices[sel];
    auto& item = d.blockItems[realIdx];

    SetWindowTextW(d.hEditSearch, L"");
    ShowWindow(d.hSuggestions, SW_HIDE);

    for (int i = 0; i < (int)d.categories.size(); i++) {
        if (!d.categories[i].isHeader && d.categories[i].typeId == item.type) {
            SendMessage(d.hComboCategory, CB_SETCURSEL, i, 0);
            break;
        }
    }
    RefreshFilter(d);
    PopulateListView(d);

    for (int i = 0; i < (int)d.displayIndices.size(); i++) {
        if (d.displayIndices[i] == realIdx) {
            ListView_SetItemState(d.hListItems, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(d.hListItems, i, FALSE);
            break;
        }
    }
}

// ポップアップ候補ListBoxのサブクラスProc
static WNDPROC s_origSuggestProc = nullptr;
static LRESULT CALLBACK SuggestSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_LBUTTONUP) {
        // 親DlgDataを取得してクリック処理
        HWND hParent = GetParent(hwnd);
        DlgData* d = (DlgData*)GetWindowLongPtrW(hParent, GWLP_USERDATA);
        if (d) {
            LRESULT res = CallWindowProcW(s_origSuggestProc, hwnd, msg, wParam, lParam);
            OnSuggestionClicked(*d);
            return res;
        }
    }
    return CallWindowProcW(s_origSuggestProc, hwnd, msg, wParam, lParam);
}

static void ReloadProfileCombo(DlgData& d, const std::wstring& selectProfile) {
    SendMessage(d.hComboProfile, CB_RESETCONTENT, 0, 0);
    SendMessage(d.hComboProfile, CB_ADDSTRING, 0, (LPARAM)L"新規作成...");
    auto profiles = d.manager->GetProfiles();
    int selIdx = 1;
    for (int i = 0; i < (int)profiles.size(); i++) {
        SendMessage(d.hComboProfile, CB_ADDSTRING, 0, (LPARAM)profiles[i].c_str());
        if (profiles[i] == selectProfile) selIdx = i + 1;
    }
    if (SendMessage(d.hComboProfile, CB_GETCOUNT, 0, 0) > selIdx)
        SendMessage(d.hComboProfile, CB_SETCURSEL, selIdx, 0);
    else if (SendMessage(d.hComboProfile, CB_GETCOUNT, 0, 0) > 1)
        SendMessage(d.hComboProfile, CB_SETCURSEL, 1, 0);
}

static void LoadItemsForProfile(DlgData& d, const std::wstring& profile) {
    d.isLoading = true;

    // ItemDatLoaderから全ブロック取得
    d.blockItems.clear();
    auto allBlocks = d.itemDatLoader->GetAllBlocksOrdered();
    for (const auto& b : allBlocks) {
        DlgData::BlockItem bi;
        bi.id = b.id;
        bi.type = b.type;
        bi.name = b.name;
        bi.basePrice = b.basePrice;
        d.blockItems.push_back(bi);
    }

    // DC.dat 読み込み
    try {
        auto bytes = d.manager->ReadDcBytes(profile);
        d.dcBytes.resize(d.blockItems.size(), 0);
        size_t copyLen = (std::min)(bytes.size(), d.dcBytes.size());
        if (copyLen > 0) std::memcpy(d.dcBytes.data(), bytes.data(), copyLen);
    } catch (...) {
        d.dcBytes.assign(d.blockItems.size(), 0);
    }

    // カテゴリコンボ構築
    SendMessage(d.hComboCategory, CB_RESETCONTENT, 0, 0);
    d.categories.clear();

    struct CatGroup { std::wstring name; std::vector<int> ids; };
    std::vector<CatGroup> groups = {
        {L"武器", {18,20,28,26,21,22,23,24,29,25,4,30,32,33,54,55,56}},
        {L"防具/補助系", {8,0,1,10,11,6,2,5,3,16,17,7,9,19,27,31,12,13,14,15}},
        {L"その他", {34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52}}
    };

    std::set<int> presentTypes;
    for (const auto& b : d.blockItems) {
        if (b.type >= 0) presentTypes.insert(b.type);
    }

    for (const auto& grp : groups) {
        DlgData::CatEntry header;
        header.name = L"── " + grp.name + L" ──";
        header.isHeader = true;
        d.categories.push_back(header);
        SendMessage(d.hComboCategory, CB_ADDSTRING, 0, (LPARAM)header.name.c_str());

        for (int typeId : grp.ids) {
            if (presentTypes.count(typeId)) {
                auto typeName = GetTypeName(typeId);
                DlgData::CatEntry ce;
                ce.name = typeName;
                ce.isHeader = false;
                ce.typeId = typeId;
                d.categories.push_back(ce);
                SendMessage(d.hComboCategory, CB_ADDSTRING, 0, (LPARAM)typeName.c_str());
            }
        }
    }

    // 最初の非ヘッダー項目を選択
    for (int i = 0; i < (int)d.categories.size(); i++) {
        if (!d.categories[i].isHeader) {
            SendMessage(d.hComboCategory, CB_SETCURSEL, i, 0);
            break;
        }
    }

    RefreshFilter(d);
    PopulateListView(d);
    ExportToGame(d);

    // 特殊2 ツールチップ動的更新
    if (d.hToolTip) {
        auto& sp = GetSpecialLists();
        auto it = sp.find(L"特殊2");
        if (it != sp.end()) {
            std::set<int> idSet(it->second.begin(), it->second.end());
            std::wstring tip;
            for (auto& b : d.blockItems) {
                if (idSet.count(b.id) && !b.name.empty()) {
                    if (!tip.empty()) tip += L", ";
                    tip += b.name;
                }
            }
            if (tip.size() > 300) { tip.resize(300); tip += L"..."; }
            d.sp2TipText = tip;
            HWND hSp2 = GetDlgItem(d.hDlg, IDC_DC_BTN_SPECIAL_BASE + 8);
            if (hSp2) UpdateTip(d.hToolTip, d.hDlg, hSp2, d.sp2TipText.c_str());
        }
    }

    d.isLoading = false;
    SetWindowTextW(d.hStatus, (L"プロファイル: " + profile + L" (" + std::to_wstring(d.blockItems.size()) + L" ブロック)").c_str());
}

// ── ON/OFF/キャンセル 3ボタンダイアログ (C# ShowOnOffCancelDialog 相当) ──
// 戻り値: IDYES=ON, IDNO=OFF, IDCANCEL=キャンセル
struct OnOffDlgData { const wchar_t* msg; const wchar_t* title; bool large; };

static INT_PTR CALLBACK OnOffDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        { HICON hIco = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON)); if (hIco) { SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIco); SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco); } }
        auto* data = (OnOffDlgData*)lp;
        SetWindowTextW(hDlg, data->title);
        HFONT hFont = ThemeManager::CreateDefaultFont();
        RECT rc; GetClientRect(hDlg, &rc);
        int cw = rc.right, ch = rc.bottom;

        if (data->large) {
            // 大きなテキストボックス付き
            HWND hTb = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", data->msg,
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                12, 12, cw - 24, ch - 96, hDlg, nullptr, nullptr, nullptr);
            SendMessage(hTb, WM_SETFONT, (WPARAM)hFont, TRUE);

            int btnTop = ch - 64;
            int center = cw / 2;
            HWND hOn = CreateWindowExW(0, L"BUTTON", L"ON", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                center - 150, btnTop, 100, 28, hDlg, (HMENU)IDYES, nullptr, nullptr);
            HWND hOff = CreateWindowExW(0, L"BUTTON", L"OFF", WS_CHILD | WS_VISIBLE,
                center - 30, btnTop, 100, 28, hDlg, (HMENU)IDNO, nullptr, nullptr);
            HWND hCan = CreateWindowExW(0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE,
                center + 90, btnTop, 100, 28, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);
            for (HWND h : {hOn, hOff, hCan}) SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        } else {
            HWND hLbl = CreateWindowExW(0, L"STATIC", data->msg,
                WS_CHILD | WS_VISIBLE, 12, 12, cw - 24, 36, hDlg, nullptr, nullptr, nullptr);
            SendMessage(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hOn = CreateWindowExW(0, L"BUTTON", L"ON", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                100, 58, 100, 28, hDlg, (HMENU)IDYES, nullptr, nullptr);
            HWND hOff = CreateWindowExW(0, L"BUTTON", L"OFF", WS_CHILD | WS_VISIBLE,
                210, 58, 100, 28, hDlg, (HMENU)IDNO, nullptr, nullptr);
            HWND hCan = CreateWindowExW(0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE,
                320, 58, 100, 28, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);
            for (HWND h : {hOn, hOff, hCan}) SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
            // ボタン下端(y=58+h=28=86px) + 下マージン12px = 98px に合わせてウィンドウ高を詰める
            {
                const int desiredClientH = 98;
                RECT rcCli; GetClientRect(hDlg, &rcCli);
                if (rcCli.bottom > desiredClientH) {
                    RECT rcWnd; GetWindowRect(hDlg, &rcWnd);
                    int newH = (rcWnd.bottom - rcWnd.top) - (rcCli.bottom - desiredClientH);
                    SetWindowPos(hDlg, nullptr, 0, 0, rcWnd.right - rcWnd.left, newH, SWP_NOMOVE | SWP_NOZORDER);
                }
            }
        }
        CenterWindowOnParent(hDlg, GetParent(hDlg));
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDYES || id == IDNO || id == IDCANCEL) { EndDialog(hDlg, id); return TRUE; }
    }
    if (msg == WM_CLOSE) { EndDialog(hDlg, IDCANCEL); return TRUE; }
    return FALSE;
}

static int ShowOnOffCancelDialog(HWND hParent, const wchar_t* message, const wchar_t* title, bool large = false) {
    OnOffDlgData odd = { message, title, large };
    int formW = large ? 640 : 520;
    int formH = large ? 320 : 140;

    // ピクセル→ダイアログ単位変換 (概算: DLU = px * 4 / avgCharW, px * 8 / charH)
    // 固定比率で近似: cx ≈ px*4/7, cy ≈ px*8/14
    WORD dlgBuf[512] = {};
    DLGTEMPLATE* pDlg = (DLGTEMPLATE*)dlgBuf;
    pDlg->style = DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    pDlg->cdit = 0;
    pDlg->cx = (short)(formW * 4 / 7);
    pDlg->cy = (short)(formH * 8 / 14);
    WORD* p = (WORD*)(pDlg + 1);
    *p++ = 0; *p++ = 0; *p++ = 0;

    return (int)DialogBoxIndirectParamW(GetModuleHandleW(nullptr), pDlg, hParent, OnOffDlgProc, (LPARAM)&odd);
}

// ── 特殊/特殊2 専用確認ダイアログ (ListView付き, C# ShowSpecialConfirmDialog 相当) ──
struct SpecialItem { int id; std::wstring name; int req; int drop; };
struct SpecialDlgData { const wchar_t* msg; const wchar_t* title; std::vector<SpecialItem> items; };

static INT_PTR CALLBACK SpecialConfirmDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) {
        { HICON hIco = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON)); if (hIco) { SendMessage(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIco); SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIco); } }
        auto* data = (SpecialDlgData*)lp;
        SetWindowTextW(hDlg, data->title);
        HFONT hFont = ThemeManager::CreateDefaultFont();

        int padding = 8;
        // ラベル
        HWND hLbl = CreateWindowExW(0, L"STATIC", data->msg,
            WS_CHILD | WS_VISIBLE, padding, padding, 400, 36, hDlg, nullptr, nullptr, nullptr);
        SendMessage(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

        // ListView (Report mode)
        int lvTop = padding + 36 + 4;
        HWND hLv = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER,
            padding, lvTop, 400, 120, hDlg, (HMENU)2001, nullptr, nullptr);
        SendMessage(hLv, WM_SETFONT, (WPARAM)hFont, TRUE);
        ListView_SetExtendedListViewStyle(hLv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        // カラム追加
        const wchar_t* headers[] = { L"ID", L"名称", L"要求Lv", L"DropLv" };
        for (int c = 0; c < 4; c++) {
            LVCOLUMNW col = {};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = const_cast<wchar_t*>(headers[c]);
            col.cx = (c == 1) ? 160 : 60;
            ListView_InsertColumn(hLv, c, &col);
        }

        // アイテム追加
        for (int i = 0; i < (int)data->items.size(); i++) {
            auto& it = data->items[i];
            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = i;
            auto idStr = std::to_wstring(it.id);
            lvi.pszText = const_cast<wchar_t*>(idStr.c_str());
            ListView_InsertItem(hLv, &lvi);
            ListView_SetItemText(hLv, i, 1, const_cast<wchar_t*>(it.name.c_str()));
            auto reqStr = (it.req <= 0) ? std::wstring(L"-") : std::to_wstring(it.req);
            ListView_SetItemText(hLv, i, 2, const_cast<wchar_t*>(reqStr.c_str()));
            auto dropStr = (it.drop <= 0 || it.drop == 10000) ? std::wstring(L"-") : std::to_wstring(it.drop);
            ListView_SetItemText(hLv, i, 3, const_cast<wchar_t*>(dropStr.c_str()));
        }

        // カラム幅をテキストに合わせて自動調整
        HDC hdc = GetDC(hLv);
        HFONT hOld = (HFONT)SelectObject(hdc, hFont);
        int colWidths[4] = {};
        for (int c = 0; c < 4; c++) {
            SIZE sz; GetTextExtentPoint32W(hdc, headers[c], (int)wcslen(headers[c]), &sz);
            colWidths[c] = sz.cx;
            for (int r = 0; r < (int)data->items.size(); r++) {
                wchar_t buf[256] = {};
                ListView_GetItemText(hLv, r, c, buf, 256);
                GetTextExtentPoint32W(hdc, buf, (int)wcslen(buf), &sz);
                if (sz.cx > colWidths[c]) colWidths[c] = sz.cx;
            }
            colWidths[c] += 16;
        }
        SelectObject(hdc, hOld);
        ReleaseDC(hLv, hdc);

        int totalW = 0;
        for (int c = 0; c < 4; c++) {
            ListView_SetColumnWidth(hLv, c, colWidths[c]);
            totalW += colWidths[c];
        }

        // ListView/フォーム幅をコンテンツに合わせる
        int lvW = totalW + 4;
        int itemH = 18;
        int headerH = 24;
        int lvH = headerH + itemH * (int)data->items.size() + 4;

        // ボタン3個+gap が収まる最低幅を保証
        int btnW = 100, gap = 20;
        int minFormCW = btnW * 3 + gap * 2 + padding * 2 + 24;
        int formCW = lvW + padding * 2 + 48;
        if (formCW < minFormCW) {
            formCW = minFormCW;
            lvW = formCW - padding * 2 - 48;
        }
        MoveWindow(hLv, padding, lvTop, lvW, lvH, TRUE);
        int btnTop = lvTop + lvH + 8;
        int formCH = btnTop + 48 + padding;

        // フォーム全体をリサイズ
        RECT wr = {0, 0, formCW, formCH};
        AdjustWindowRectEx(&wr, DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, 0);
        SetWindowPos(hDlg, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);

        // ラベル幅を合わせる
        MoveWindow(hLbl, padding, padding, formCW - padding * 2, 36, TRUE);

        // ボタン (ON / OFF / キャンセル) — 中央寄せ
        int btnH = 28;
        int totalBtnW = btnW * 3 + gap * 2;
        int btnLeft = (formCW - totalBtnW) / 2;
        HWND hOn = CreateWindowExW(0, L"BUTTON", L"ON", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            btnLeft, btnTop, btnW, btnH, hDlg, (HMENU)IDYES, nullptr, nullptr);
        HWND hOff = CreateWindowExW(0, L"BUTTON", L"OFF", WS_CHILD | WS_VISIBLE,
            btnLeft + btnW + gap, btnTop, btnW, btnH, hDlg, (HMENU)IDNO, nullptr, nullptr);
        HWND hCan = CreateWindowExW(0, L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE,
            btnLeft + (btnW + gap) * 2, btnTop, btnW, btnH, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);
        for (HWND h : {hOn, hOff, hCan}) SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 中央配置（画面外防止付き）
        CenterWindowOnParent(hDlg, GetParent(hDlg));

        return TRUE;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDYES || id == IDNO || id == IDCANCEL) { EndDialog(hDlg, id); return TRUE; }
    }
    if (msg == WM_CLOSE) { EndDialog(hDlg, IDCANCEL); return TRUE; }
    return FALSE;
}

static int ShowSpecialConfirmDialog(HWND hParent, const wchar_t* message, const wchar_t* title,
                                     const std::vector<SpecialItem>& items) {
    SpecialDlgData sdd = { message, title, items };

    WORD dlgBuf[512] = {};
    DLGTEMPLATE* pDlg = (DLGTEMPLATE*)dlgBuf;
    pDlg->style = DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    pDlg->cdit = 0;
    pDlg->cx = 200; pDlg->cy = 150; // 初期サイズ (WM_INITDIALOGで再計算)
    WORD* p = (WORD*)(pDlg + 1);
    *p++ = 0; *p++ = 0; *p++ = 0;

    return (int)DialogBoxIndirectParamW(GetModuleHandleW(nullptr), pDlg, hParent, SpecialConfirmDlgProc, (LPARAM)&sdd);
}

// ── 特殊リスト適用 ──
static void ApplySpecialList(DlgData& d, const std::wstring& name) {
    auto& specialLists = GetSpecialLists();
    std::vector<int> ids;

    if (name == L"10万") {
        std::set<int> excludeSet;
        for (const auto& [k, v] : specialLists) {
            if (k != L"10万") {
                for (int id : v) excludeSet.insert(id);
            }
        }
        for (const auto& b : d.blockItems) {
            if (b.basePrice >= 100000 && excludeSet.find(b.id) == excludeSet.end()) {
                ids.push_back(b.id);
            }
        }
        if (ids.empty()) {
            MessageBoxW(d.hDlg, L"10万リストに該当するアイテムが見つかりません。\nitem.datが正しく読み込まれているか確認してください。",
                L"情報", MB_OK | MB_ICONINFORMATION);
            return;
        }
    } else {
        auto it = specialLists.find(name);
        if (it == specialLists.end() || it->second.empty()) {
            MessageBoxW(d.hDlg, L"未定義または空の特殊リストです。", L"情報", MB_OK | MB_ICONINFORMATION);
            return;
        }
        ids = it->second;
    }

    // ── 確認ダイアログの分岐 ──
    int result;
    std::wstring msg = name + L" リストのみを一括変更保存します。下記ボタンを選択してください";

    if (name == L"特殊") {
        // 専用ダイアログ (ListView付き) — 固定の4アイテム
        std::vector<SpecialItem> items = {
            {617,  L"魔除け",                      1100, 1100},
            {1744, L"アルケミストアロー",           1500, 1500},
            {1869, L"[RMU]バハムートの鉤爪",       1500, 1500},
            {1878, L"パイレーツハンター・レガシー",  1900, 1500},
            {3669, L"[JMU]百裂極爪",               1500, 1500},
            {3679, L"グランド・オーダー",             1500, 1500},
            {3680, L"凶兆の冥衣",                    1500, 1500}
        };
        result = ShowSpecialConfirmDialog(d.hDlg,
            L"特殊リストの以下アイテムを一括変換保存します。\nよろしいですか？", L"確認", items);

    } else if (name == L"特殊2") {
        // 専用ダイアログ (ListView付き) — アイテム名はitem.datから動的取得
        const int sp2Ids[] = {5015, 5047, 5044, 5045, 5046};
        const int sp2Reqs[] = {0, 0, 1000, 1000, 1000};
        const int sp2Drops[] = {1, 10000, 10000, 10000, 10000};
        std::vector<SpecialItem> items;
        for (int i = 0; i < 5; i++) {
            std::wstring itemName = std::to_wstring(sp2Ids[i]);
            for (const auto& b : d.blockItems) {
                if (b.id == sp2Ids[i] && !b.name.empty()) { itemName = b.name; break; }
            }
            items.push_back({sp2Ids[i], itemName, sp2Reqs[i], sp2Drops[i]});
        }
        result = ShowSpecialConfirmDialog(d.hDlg,
            L"特殊2リストの以下アイテムを一括変換保存します。\nよろしいですか？", L"確認", items);

    } else {
        // ノーマルU/DXU/BFU/800/900/1000/2100/10万 → ON/OFF/キャンセル
        result = ShowOnOffCancelDialog(d.hDlg, msg.c_str(), L"確認", false);
    }

    if (result == IDCANCEL) return;
    bool setOn = (result == IDYES);

    std::set<int> idSet(ids.begin(), ids.end());
    for (int i = 0; i < (int)d.blockItems.size(); i++) {
        if (idSet.count(d.blockItems[i].id)) {
            d.dcBytes[i] = setOn ? 1 : 0;
        }
    }
    SaveCurrentDc(d);
    PopulateListView(d);
    SetWindowTextW(d.hStatus, (name + L" を" + (setOn ? L"ON" : L"OFF") + L"適用しました").c_str());
}

static const wchar_t* DC_DLG_CLASS = L"JRSChatDCSettingDlg";

static LRESULT CALLBACK DcDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DlgData* d = (DlgData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        auto cs = (CREATESTRUCTW*)lParam;
        d = (DlgData*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        d->hDlg = hwnd;
        { HICON hIco = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON)); if (hIco) { SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIco); SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIco); } }

        int cw = 620, y = 8;
        HFONT hFont = ThemeManager::CreateDefaultFont();
        const int panelX = 8;
        const int innerPad = 8;  // パネル内パディング

        // ═══ パネル1: 選択中DC + 注記 (枠付き) ═══
        {
            int panelY = y;
            int py = innerPad; // パネル内Y
            // 「選択中DC:」ラベル + コンボ (中央寄せ)
            int lblW = 75, comboW = 160;
            int centerX = (cw - lblW - comboW) / 2;
            CreateWindowExW(0, L"STATIC", L"選択中DC：", WS_CHILD | WS_VISIBLE | SS_LEFT,
                panelX + centerX, 0, lblW, 20, hwnd, nullptr, nullptr, nullptr);
            d->hComboProfile = CreateWindowExW(0, L"COMBOBOX", nullptr,
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                panelX + centerX + lblW, 0, comboW, 200, hwnd, (HMENU)IDC_DC_COMBO_PROFILE, nullptr, nullptr);
            py += 26;
            // 注記
            auto hNote = CreateWindowExW(0, L"STATIC",
                L"切り替えや保存後はゲーム内のDropClean(OrbStyle)をOFF>ON切り替えで反映されます",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                panelX + innerPad, 0, cw - innerPad * 2, 18, hwnd, nullptr, nullptr, nullptr);
            py += 22;
            int panelH = py + innerPad;
            // 枠パネル (STATIC + SS_SUNKEN で枠線)
            HWND hPanel1 = CreateWindowExW(WS_EX_CLIENTEDGE | WS_EX_TRANSPARENT, L"STATIC", nullptr,
                WS_CHILD | WS_VISIBLE | SS_SIMPLE | WS_DISABLED,
                panelX, panelY, cw, panelH, hwnd, nullptr, nullptr, nullptr);
            // 子コントロールの位置を実際のパネル位置に調整
            // ラベル+コンボ
            MoveWindow(GetDlgItem(hwnd, IDC_DC_COMBO_PROFILE), panelX + centerX + lblW, panelY + innerPad + 2, comboW, 200, FALSE);
            SetWindowPos(FindWindowExW(hwnd, nullptr, L"STATIC", L"選択中DC："), nullptr,
                panelX + centerX, panelY + innerPad + 4, lblW, 20, SWP_NOZORDER);
            MoveWindow(hNote, panelX + innerPad, panelY + innerPad + 26, cw - innerPad * 2, 18, FALSE);
            // パネル自体を背面へ
            SetWindowPos(hPanel1, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            y = panelY + panelH + 6;
        }

        // ═══ パネル2: カテゴリ + 検索 + クイックボタン (枠付き) ═══
        {
            int panelY = y;
            int py = innerPad;
            // 1行目: カテゴリ + 検索
            CreateWindowExW(0, L"STATIC", L"カテゴリ:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                panelX + innerPad, panelY + py + 3, 60, 20, hwnd, nullptr, nullptr, nullptr);
            d->hComboCategory = CreateWindowExW(0, L"COMBOBOX", nullptr,
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                panelX + innerPad + 62, panelY + py, 180, 300, hwnd, (HMENU)IDC_DC_COMBO_CATEGORY, nullptr, nullptr);

            CreateWindowExW(0, L"STATIC", L"検索:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                panelX + innerPad + 260, panelY + py + 3, 40, 20, hwnd, nullptr, nullptr, nullptr);
            d->hEditSearch = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                panelX + innerPad + 302, panelY + py, 240, 22, hwnd, (HMENU)IDC_DC_EDIT_SEARCH, nullptr, nullptr);
            py += 28;

            // 2行目: クイックボタン
            {
                CreateWindowExW(0, L"STATIC", L"クイック:", WS_CHILD | WS_VISIBLE | SS_LEFT,
                    panelX + innerPad, panelY + py + 3, 60, 20, hwnd, nullptr, nullptr, nullptr);
                int qx = panelX + innerPad + 62;
                struct { const wchar_t* text; int id; } qBtns[] = {
                    {L"Gold", IDC_DC_BTN_QUICK_GOLD},
                    {L"冒コイン", IDC_DC_BTN_QUICK_COIN},
                    {L"地下界", IDC_DC_BTN_QUICK_CHIKA},
                    {L"バッジ類", IDC_DC_BTN_QUICK_BADGE}
                };
                for (auto& qb : qBtns) {
                    auto h = CreateWindowExW(0, L"BUTTON", qb.text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                        qx, panelY + py, 84, 24, hwnd, (HMENU)(INT_PTR)qb.id, nullptr, nullptr);
                    SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
                    qx += 88;
                }
            }
            py += 28;

            int panelH = py + innerPad;
            HWND hPanel2 = CreateWindowExW(WS_EX_CLIENTEDGE | WS_EX_TRANSPARENT, L"STATIC", nullptr,
                WS_CHILD | WS_VISIBLE | SS_SIMPLE | WS_DISABLED,
                panelX, panelY, cw, panelH, hwnd, nullptr, nullptr, nullptr);
            SetWindowPos(hPanel2, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            y = panelY + panelH + 6;
        }

        // ═══ パネル3: 一括ボタン + 特殊ボタン (枠付き) ═══
        {
            int panelY = y;
            int py = innerPad;

            // テキスト幅計測用
            HDC hdc = GetDC(hwnd);
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
            auto measureBtn = [&](const wchar_t* text) -> int {
                SIZE sz = {};
                GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
                return (std::max)(48, (int)sz.cx + 12);
            };

            // 一括ボタン行
            CreateWindowExW(0, L"STATIC", L"一括切り替え：", WS_CHILD | WS_VISIBLE | SS_LEFT,
                panelX + innerPad, panelY + py + 3, 80, 20, hwnd, nullptr, nullptr, nullptr);
            int bx = panelX + innerPad + 82;
            auto mkBtn = [&](const wchar_t* text, int id) {
                int w = measureBtn(text);
                auto h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    bx, panelY + py, w, 24, hwnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
                bx += w + 4;
            };
            mkBtn(L"表示中のみON", IDC_DC_BTN_CAT_ON);
            mkBtn(L"表示中のみOFF", IDC_DC_BTN_CAT_OFF);
            mkBtn(L"全てON", IDC_DC_BTN_ALL_ON);
            mkBtn(L"全てOFF", IDC_DC_BTN_ALL_OFF);
            py += 28;

            // 特殊ボタン行
            bx = panelX + innerPad;
            const wchar_t* specialNames[] = {L"ノーマルU", L"DXU", L"BFU", L"800", L"900", L"1000", L"2100", L"特殊", L"特殊2", L"10万"};
            for (int i = 0; i < 10; i++) {
                int w = measureBtn(specialNames[i]);
                auto h = CreateWindowExW(0, L"BUTTON", specialNames[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    bx, panelY + py, w, 24, hwnd, (HMENU)(INT_PTR)(IDC_DC_BTN_SPECIAL_BASE + i), nullptr, nullptr);
                SendMessage(h, WM_SETFONT, (WPARAM)hFont, TRUE);
                bx += w + 3;
            }
            SelectObject(hdc, hOldFont);
            ReleaseDC(hwnd, hdc);
            py += 28;

            int panelH = py + innerPad;
            HWND hPanel3 = CreateWindowExW(WS_EX_CLIENTEDGE | WS_EX_TRANSPARENT, L"STATIC", nullptr,
                WS_CHILD | WS_VISIBLE | SS_SIMPLE | WS_DISABLED,
                panelX, panelY, cw, panelH, hwnd, nullptr, nullptr, nullptr);
            SetWindowPos(hPanel3, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            y = panelY + panelH + 6;
        }

        // ツールチップ作成
        d->hToolTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowPos(d->hToolTip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SendMessageW(d->hToolTip, TTM_SETMAXTIPWIDTH, 0, 400);
        SendMessageW(d->hToolTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);
        SendMessageW(d->hToolTip, TTM_ACTIVATE, TRUE, 0);
        {
            // 特殊ボタンのツールチップ
            struct { int idx; const wchar_t* tip; } tips[] = {
                {2, L"BFU: 要求800,ドロップLv1000"},
                {3, L"800DXU: 要求800,ドロップLv1000"},
                {4, L"900UMU: 要求900,ドロップLv1000(ネイチャーはDLv1200)"},
                {5, L"1000UMU: 要求1000,ドロップLv1100"},
                {6, L"2100UMU: 要求2100,ドロップLv2026"},
                {7, L"魔除け,アルケミストアロー,[RMU]バハムートの鉤爪,パイレーツハンター・レガシー,[JMU]百裂極爪"},
                {9, L"Base_price10万ゴールド以上のみ（U/DXU/ノーマルU/DXU/BFU/特殊/特殊2は除外）"},
            };
            for (auto& t : tips) {
                HWND hBtn = GetDlgItem(hwnd, IDC_DC_BTN_SPECIAL_BASE + t.idx);
                if (hBtn) SetTip(d->hToolTip, hwnd, hBtn, t.tip);
            }
            // 特殊2 は動的に更新するので空で登録
            HWND hSp2 = GetDlgItem(hwnd, IDC_DC_BTN_SPECIAL_BASE + 8);
            if (hSp2) SetTip(d->hToolTip, hwnd, hSp2, L"");
        }

        // 注意ラベル
        auto hNotice = CreateWindowExW(0, L"STATIC",
            L"チェック空(OFF)=アイテム表示 / チェック入り(ON)=アイテム非表示",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            8, y, cw, 18, hwnd, nullptr, nullptr, nullptr);
        SendMessage(hNotice, WM_SETFONT, (WPARAM)hFont, TRUE);
        y += 22;

        // ListView (チェックボックス付き)
        d->hListItems = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_NOCOLUMNHEADER | WS_BORDER,
            8, y, cw, 300, hwnd, (HMENU)IDC_DC_LIST_ITEMS, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(d->hListItems,
            LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
        SendMessage(d->hListItems, WM_SETFONT, (WPARAM)hFont, TRUE);
        {
            RECT lvrc; GetClientRect(d->hListItems, &lvrc);
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;
            col.pszText = const_cast<wchar_t*>(L"アイテム名");
            col.cx = lvrc.right;
            ListView_InsertColumn(d->hListItems, 0, &col);
        }

        // ステータスバー
        d->hStatus = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            8, y + 306, cw, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessage(d->hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 検索候補ListBox (初期非表示、ポップアップで最前面)
        d->hSuggestions = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"LISTBOX", nullptr,
            WS_POPUP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 280, 120, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessage(d->hSuggestions, WM_SETFONT, (WPARAM)hFont, TRUE);
        // サブクラス化してクリック検知
        s_origSuggestProc = (WNDPROC)SetWindowLongPtrW(d->hSuggestions, GWLP_WNDPROC, (LONG_PTR)SuggestSubclassProc);
        SetWindowLongPtrW(d->hSuggestions, GWLP_HWNDPARENT, (LONG_PTR)hwnd);

        // 全子コントロールにフォント適用
        ThemeManager::ApplyFontToChildren(hwnd, hFont);

        // プロファイル読み込み
        {
            d->manager->EnsureDcRootExists();
            auto profiles = d->manager->GetProfiles();
            if (profiles.empty()) {
                d->manager->LoadOrCreateOriginal(*d->settings);
                profiles = d->manager->GetProfiles();
            }
            SendMessage(d->hComboProfile, CB_ADDSTRING, 0, (LPARAM)L"新規作成...");
            for (const auto& p : profiles) {
                SendMessage(d->hComboProfile, CB_ADDSTRING, 0, (LPARAM)p.c_str());
            }
            // 設定済みプロファイルを選択
            int selIdx = 1;
            if (!d->settings->dcProfile.empty()) {
                for (int i = 0; i < (int)profiles.size(); i++) {
                    if (profiles[i] == d->settings->dcProfile) { selIdx = i + 1; break; }
                }
            }
            if (SendMessage(d->hComboProfile, CB_GETCOUNT, 0, 0) > selIdx)
                SendMessage(d->hComboProfile, CB_SETCURSEL, selIdx, 0);
            else if (SendMessage(d->hComboProfile, CB_GETCOUNT, 0, 0) > 1)
                SendMessage(d->hComboProfile, CB_SETCURSEL, 1, 0);

            // 初期プロファイル読み込み
            wchar_t profBuf[256];
            GetWindowTextW(d->hComboProfile, profBuf, 256);
            std::wstring initProfile(profBuf);
            if (!initProfile.empty() && initProfile != L"新規作成...") {
                LoadItemsForProfile(*d, initProfile);
            }
        }

        return 0;
    }

    case WM_SIZE: {
        if (!d) break;
        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right - 16;
        // ListView のリサイズ
        if (d->hListItems) {
            RECT lvRC;
            GetWindowRect(d->hListItems, &lvRC);
            POINT pt = {lvRC.left, lvRC.top};
            ScreenToClient(hwnd, &pt);
            int lvH = rc.bottom - pt.y - 28;
            if (lvH < 50) lvH = 50;
            MoveWindow(d->hListItems, 8, pt.y, cw, lvH, TRUE);
            if (d->hStatus) MoveWindow(d->hStatus, 8, pt.y + lvH + 4, cw, 20, TRUE);
        }
        return 0;
    }

    case WM_COMMAND: {
        if (!d) break;
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_DC_COMBO_PROFILE && code == CBN_SELCHANGE) {
            int sel = (int)SendMessage(d->hComboProfile, CB_GETCURSEL, 0, 0);
            if (sel == 0) {
                // 新規作成: テキスト入力ダイアログ表示
                auto name = ShowInputDialog(hwnd,
                    L"自分が分かりやすい設定名を新規フォルダ名として\r\n記入してください（例： Uのみ）",
                    L"新規作成");
                if (!name.empty()) {
                    auto profiles = d->manager->GetProfiles();
                    if (profiles.size() >= 10) {
                        MessageBoxW(hwnd, L"プロファイルは最大10件までです。", L"エラー", MB_OK | MB_ICONWARNING);
                        ReloadProfileCombo(*d, d->settings->dcProfile);
                    } else {
                        bool exists = false;
                        for (const auto& p : profiles) { if (p == name) { exists = true; break; } }
                        if (exists) {
                            MessageBoxW(hwnd, L"同じ名前のプロファイルが既に存在します。", L"エラー", MB_OK | MB_ICONWARNING);
                            ReloadProfileCombo(*d, d->settings->dcProfile);
                        } else {
                            auto src = d->settings->dcProfile;
                            if (src.empty()) src = profiles.empty() ? L"オリジナル" : profiles[0];
                            if (d->manager->CreateProfileFromExisting(name, src)) {
                                d->settings->dcProfile = name;
                                d->settings->Save();
                                ReloadProfileCombo(*d, name);
                                LoadItemsForProfile(*d, name);
                            } else {
                                MessageBoxW(hwnd, L"プロファイルの作成に失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
                                ReloadProfileCombo(*d, d->settings->dcProfile);
                            }
                        }
                    }
                } else {
                    // キャンセル→元のプロファイルに戻す
                    ReloadProfileCombo(*d, d->settings->dcProfile);
                }
            } else {
                wchar_t buf[256];
                SendMessageW(d->hComboProfile, CB_GETLBTEXT, sel, (LPARAM)buf);
                std::wstring profile(buf);
                d->settings->dcProfile = profile;
                d->settings->Save();
                LoadItemsForProfile(*d, profile);
            }
            return 0;
        }

        if (id == IDC_DC_COMBO_CATEGORY && code == CBN_SELCHANGE) {
            int sel = (int)SendMessage(d->hComboCategory, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)d->categories.size() && d->categories[sel].isHeader) {
                // ヘッダー選択は次の非ヘッダーへ飛ばす
                for (int i = sel + 1; i < (int)d->categories.size(); i++) {
                    if (!d->categories[i].isHeader) {
                        SendMessage(d->hComboCategory, CB_SETCURSEL, i, 0);
                        break;
                    }
                }
            }
            RefreshFilter(*d);
            PopulateListView(*d);
            return 0;
        }

        if (id == IDC_DC_EDIT_SEARCH && code == EN_CHANGE) {
            // 検索候補ドロップダウン更新
            {
                ShowWindow(d->hSuggestions, SW_HIDE);
                d->suggestionIndices.clear();
                SendMessage(d->hSuggestions, LB_RESETCONTENT, 0, 0);

                wchar_t qBuf[256] = {};
                GetWindowTextW(d->hEditSearch, qBuf, 256);
                std::wstring q(qBuf);
                // trim
                while (!q.empty() && (q.front() == L' ' || q.front() == L'\t')) q.erase(q.begin());
                while (!q.empty() && (q.back() == L' ' || q.back() == L'\t')) q.pop_back();

                if (!q.empty() && !d->blockItems.empty()) {
                    std::wstring lq = q;
                    std::transform(lq.begin(), lq.end(), lq.begin(), ::towlower);
                    int count = 0;
                    for (int i = 0; i < (int)d->blockItems.size() && count < 10; i++) {
                        if (IsPlaceholder(d->blockItems[i])) continue;
                        std::wstring ln = d->blockItems[i].name;
                        std::transform(ln.begin(), ln.end(), ln.begin(), ::towlower);
                        if (ln.find(lq) != std::wstring::npos) {
                            SendMessage(d->hSuggestions, LB_ADDSTRING, 0, (LPARAM)d->blockItems[i].name.c_str());
                            d->suggestionIndices.push_back(i);
                            count++;
                        }
                    }
                    if (count > 0) {
                        // 検索エディットの直下にスクリーン座標で配置
                        RECT editRc;
                        GetWindowRect(d->hEditSearch, &editRc);
                        int h = (std::min)(200, 18 * count + 4);
                        SetWindowPos(d->hSuggestions, HWND_TOPMOST,
                            editRc.left, editRc.bottom,
                            editRc.right - editRc.left, h,
                            SWP_NOACTIVATE);
                        ShowWindow(d->hSuggestions, SW_SHOWNA);
                    }
                }
            }
            // 検索入力中はListView更新しない。候補クリック時のみカテゴリ切替+表示更新する。
            return 0;
        }

        // 検索候補クリックはサブクラスで処理済み (OnSuggestionClicked)

        if (id == IDC_DC_BTN_CAT_ON || id == IDC_DC_BTN_CAT_OFF) {
            bool on = (id == IDC_DC_BTN_CAT_ON);
            if (MessageBoxW(hwnd,
                on ? L"表示中のカテゴリ内を全てONにします。よろしいですか？"
                   : L"表示中のカテゴリ内を全てOFFにします。よろしいですか？",
                L"確認", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
                return 0;
            for (int idx : d->displayIndices) {
                d->dcBytes[idx] = on ? 1 : 0;
            }
            SaveCurrentDc(*d);
            PopulateListView(*d);
            SetWindowTextW(d->hStatus, L"カテゴリ内の変更を保存しました。");
            return 0;
        }

        if (id == IDC_DC_BTN_ALL_ON || id == IDC_DC_BTN_ALL_OFF) {
            bool on = (id == IDC_DC_BTN_ALL_ON);
            if (MessageBoxW(hwnd,
                on ? L"全てのアイテムをONにします。よろしいですか？"
                   : L"全てのアイテムをOFFにします。よろしいですか？",
                L"確認", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
                return 0;
            for (size_t i = 0; i < d->dcBytes.size(); i++) d->dcBytes[i] = on ? 1 : 0;
            SaveCurrentDc(*d);
            PopulateListView(*d);
            SetWindowTextW(d->hStatus, L"全件の変更を保存しました。");
            return 0;
        }

        // クイックボタン
        if (id == IDC_DC_BTN_QUICK_GOLD)  { SelectItemById(*d, 0);    return 0; }
        if (id == IDC_DC_BTN_QUICK_COIN)  { SelectItemById(*d, 5015); return 0; }
        if (id == IDC_DC_BTN_QUICK_CHIKA) { SelectItemById(*d, 5047); return 0; }
        if (id == IDC_DC_BTN_QUICK_BADGE) { SelectItemById(*d, 5044); return 0; }

        // 特殊ボタン
        if (id >= IDC_DC_BTN_SPECIAL_BASE && id < IDC_DC_BTN_SPECIAL_BASE + 10) {
            const wchar_t* names[] = {L"ノーマルU", L"DXU", L"BFU", L"800", L"900", L"1000", L"2100", L"特殊", L"特殊2", L"10万"};
            ApplySpecialList(*d, names[id - IDC_DC_BTN_SPECIAL_BASE]);
            return 0;
        }

        break;
    }

    case WM_NOTIFY: {
        if (!d) break;
        auto* nm = (NMHDR*)lParam;
        if (nm->hwndFrom == d->hListItems && nm->code == LVN_ITEMCHANGED) {
            if (d->suppressCheck) break;
            auto* nmlv = (NMLISTVIEW*)lParam;
            if ((nmlv->uChanged & LVIF_STATE) &&
                ((nmlv->uOldState & LVIS_STATEIMAGEMASK) != (nmlv->uNewState & LVIS_STATEIMAGEMASK))) {
                int item = nmlv->iItem;
                if (item >= 0 && item < (int)d->displayIndices.size()) {
                    int realIdx = d->displayIndices[item];
                    bool checked = ListView_GetCheckState(d->hListItems, item);
                    if (realIdx < (int)d->dcBytes.size()) {
                        d->dcBytes[realIdx] = checked ? 1 : 0;
                        SaveCurrentDc(*d);
                    }
                }
            }
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

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void DCSettingDialog::Show(HWND hwndParent, DCManager& manager, AppSettings& settings, ItemDatLoader& itemDatLoader) {
    // ウィンドウクラス登録（一度だけ）
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DcDlgProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = DC_DLG_CLASS;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    DlgData data;
    data.manager = &manager;
    data.settings = &settings;
    data.itemDatLoader = &itemDatLoader;

    RECT r = {0, 0, 650, 560};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME, FALSE);

    HWND hwnd = CreateWindowExW(0, DC_DLG_CLASS, L"ドロップクリーン設定",
        (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME) | WS_VISIBLE,
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
