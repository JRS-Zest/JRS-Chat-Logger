// guild_compare_window.cpp — ギルド員比較ウィンドウ
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "guild_compare_window.h"
#include "theme_manager.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <ctime>
#include <chrono>

namespace fs = std::filesystem;

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

// ───────────────────────── 定数 ─────────────────────────
static const wchar_t* kCmpClass = L"GuildCompareWnd";
static bool s_classRegistered = false;
static HWND s_hCompareWnd = nullptr;   // シングルトン

constexpr int IDC_CMP_INFO       = 100;
constexpr int IDC_CMP_FILELIST   = 101;
constexpr int IDC_CMP_LISTVIEW   = 102;
constexpr int IDC_CMP_CHANGEDONLY= 103;
constexpr int IDC_CMP_SAVE       = 104;
constexpr int IDC_CMP_GUILDCOMBO = 105;
constexpr int IDC_CMP_GUILDLABEL = 106;
constexpr int IDC_CMP_UNCHANGEDONLY = 107;
constexpr int IDC_CMP_EVENTLOG  = 108;
constexpr int IDC_CMP_FILELISTLABEL = 109;
constexpr int IDC_CMP_JOB_BASE  = 200;   // 200–218 の19個
constexpr int kJobCount = 19;

constexpr int kInfoH    = 80;   // 上部情報パネル高さ (ギルド名行のみ、職業はボタンに移動)
constexpr int kJobRowH  = 50;   // 職業ボタンエリア (2行分)
constexpr int kTopH     = 80 + 50;  // kInfoH + kJobRowH = 情報+職業ボタン全体
constexpr int kBottomH  = 32;   // 下部パネル高さ
constexpr int kFileListW = 160; // 左ファイルリスト幅
constexpr int kGuildRowH = 28;  // ギルド選択行高さ

// 職業マスターテーブル (19職業)
struct JobDef {
    const wchar_t* fullName;  // CSV上の正式名
    const wchar_t* shortName; // ボタン表示用短縮名
};
static const JobDef kJobs[kJobCount] = {
    { L"剣士",           L"剣士"  },
    { L"戦士",           L"戦士"  },
    { L"ウィザード",     L"WIZ" },
    { L"ウルフマン",     L"狼"   },
    { L"ビショップ",     L"BIS" },
    { L"追放天使",       L"天使"  },
    { L"シーフ",       L"シーフ"},
    { L"武道家",         L"武道家"},
    { L"ランサー",     L"ランサ"},
    { L"アーチャー",   L"アチャ"},
    { L"ビーストテイマー", L"テイマ"},
    { L"サマナー",     L"サマナ"},
    { L"プリンセス",   L"姫"   },
    { L"リトルウィッチ", L"リトル"},
    { L"ネクロマンサー", L"ネクロ"},
    { L"悪魔",           L"悪魔"  },
    { L"霊術師",         L"霊術師"},
    { L"闘士",           L"闘士"  },
    { L"光奏師",         L"光奏師"},
};

// ───────────────────────── データ構造 ─────────────────────────
struct CsvMember {
    std::wstring name;
    int level = 0;
    std::wstring job;
    std::wstring rank;
};

struct CsvFile {
    std::wstring filename;     // ファイル名のみ
    std::wstring fullPath;
    std::wstring guildName;
    std::wstring dateStr;      // YYYYMMDD_HHMMSS
    std::tm parsedTime{};
    std::vector<CsvMember> members;
    bool loaded = false;
};

// 比較表示用1行
struct CompareRow {
    std::wstring name;
    std::wstring level;   // 最新Lv (脱退の場合 "--")
    std::wstring job;
    std::wstring rank;
    std::wstring oldLevel; // 旧Lv ("", "加入", "脱退", 数値)
    int status = 0;        // 0=通常, 1=加入, 2=脱退, 3=レベル変化
};

// ───────────────────────── ウィンドウデータ ─────────────────────────
struct CompareWndData {
    HFONT hFont = nullptr;
    std::wstring guildFolder;
    HINSTANCE hInst = nullptr;

    // ギルド名一覧とCSVファイル
    std::vector<std::wstring> guildNames;
    std::wstring selectedGuild;
    std::vector<CsvFile> allFiles;        // 全ギルドのCSV
    std::vector<int> filteredIndices;     // 選択ギルドのファイルインデックス (新しい順)
    int latestIdx = -1;                  // filteredIndices内の最新
    int selectedFileIdx = -1;            // filteredIndices内の選択中

    // 表示データ
    std::vector<CompareRow> rows;
    bool showChangedOnly = false;
    bool showUnchangedOnly = false;
    std::wstring jobFilter;              // 空="全職業", 値="剣士"等 (fullName)
    int lvSortOrder = 0;                 // 0=なし, 1=昇順, 2=降順
    std::vector<int> visibleRows;        // rowsのインデックス (フィルタ後)

    // UI
    HWND hInfo = nullptr;
    HWND hEventLog = nullptr;
    HWND hJobBtns[kJobCount] = {};       // 職業フィルタボタン
    HWND hTooltip = nullptr;             // ツールチップ
    HWND hFileListLabel = nullptr;
    HWND hFileList = nullptr;
    HWND hListView = nullptr;
    HWND hChangedOnly = nullptr;
    HWND hUnchangedOnly = nullptr;
    HWND hSaveBtn = nullptr;
    HWND hGuildCombo = nullptr;
    HWND hGuildLabel = nullptr;
};

// ───────────────────────── CSV読み込み ─────────────────────────
static std::wstring ReadUtf8File(const std::wstring& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    // BOMスキップ
    if (raw.size() >= 3 && raw[0] == '\xEF' && raw[1] == '\xBB' && raw[2] == '\xBF')
        raw = raw.substr(3);
    // UTF-8 → wstring
    if (raw.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring ws(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), (int)raw.size(), ws.data(), wlen);
    return ws;
}

static std::vector<CsvMember> ParseCsvContent(const std::wstring& content) {
    std::vector<CsvMember> members;
    std::wistringstream iss(content);
    std::wstring line;
    bool headerSkipped = false;
    while (std::getline(iss, line)) {
        // \r を除去
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty()) continue;
        // ヘッダ行スキップ (ギルド員名,... で始まる行)
        if (!headerSkipped) {
            if (line.find(L"ギルド員名") != std::wstring::npos) {
                headerSkipped = true;
                continue;
            }
            headerSkipped = true; // ヘッダがなくてもスキップ扱い
        }
        // カンマ分割
        std::wistringstream ls(line);
        std::wstring tok;
        CsvMember m;
        int col = 0;
        while (std::getline(ls, tok, L',')) {
            switch (col) {
            case 0: m.name = tok; break;
            case 1: try { m.level = std::stoi(tok); } catch (...) { m.level = 0; } break;
            case 2: m.job = tok; break;
            case 3: m.rank = tok; break;
            }
            col++;
        }
        if (!m.name.empty()) members.push_back(std::move(m));
    }
    return members;
}

static void EnsureLoaded(CsvFile& cf) {
    if (cf.loaded) return;
    auto content = ReadUtf8File(cf.fullPath);
    cf.members = ParseCsvContent(content);
    cf.loaded = true;
}

// ───────────────────────── ファイルスキャン ─────────────────────────
static void ScanGuildFolder(CompareWndData& d) {
    d.allFiles.clear();
    d.guildNames.clear();
    if (!fs::exists(d.guildFolder)) return;

    std::set<std::wstring> guildSet;
    for (auto& entry : fs::directory_iterator(d.guildFolder)) {
        if (!entry.is_regular_file()) continue;
        auto fname = entry.path().filename().wstring();
        if (fname.size() < 20) continue;
        auto ext = fname.substr(fname.size() - 4);
        if (ext != L".csv" && ext != L".CSV") continue;

        CsvFile cf;
        cf.filename = fname;
        cf.fullPath = entry.path().wstring();
        cf.dateStr = fname.substr(0, 15); // YYYYMMDD_HHMMSS
        cf.guildName = fname.substr(16, fname.size() - 16 - 4);

        // タイムスタンプのパース
        if (cf.dateStr.size() == 15 && cf.dateStr[8] == L'_') {
            try {
                cf.parsedTime.tm_year = std::stoi(cf.dateStr.substr(0, 4)) - 1900;
                cf.parsedTime.tm_mon  = std::stoi(cf.dateStr.substr(4, 2)) - 1;
                cf.parsedTime.tm_mday = std::stoi(cf.dateStr.substr(6, 2));
                cf.parsedTime.tm_hour = std::stoi(cf.dateStr.substr(9, 2));
                cf.parsedTime.tm_min  = std::stoi(cf.dateStr.substr(11, 2));
                cf.parsedTime.tm_sec  = std::stoi(cf.dateStr.substr(13, 2));
                cf.parsedTime.tm_isdst = -1;
            } catch (...) {}
        }

        guildSet.insert(cf.guildName);
        d.allFiles.push_back(std::move(cf));
    }

    // ギルド名一覧
    for (auto& g : guildSet) d.guildNames.push_back(g);
    std::sort(d.guildNames.begin(), d.guildNames.end());
}

// 選択ギルドでフィルタリング (新しい順にソート)
static void FilterByGuild(CompareWndData& d) {
    d.filteredIndices.clear();
    for (int i = 0; i < (int)d.allFiles.size(); i++) {
        if (d.allFiles[i].guildName == d.selectedGuild)
            d.filteredIndices.push_back(i);
    }
    // 新しい順 (dateStr降順)
    std::sort(d.filteredIndices.begin(), d.filteredIndices.end(),
        [&](int a, int b) { return d.allFiles[a].dateStr > d.allFiles[b].dateStr; });
    d.latestIdx = d.filteredIndices.empty() ? -1 : 0;
    d.selectedFileIdx = -1;
}

// ───────────────────────── 比較ロジック ─────────────────────────
static void BuildCompareRows(CompareWndData& d) {
    d.rows.clear();
    d.visibleRows.clear();
    if (d.latestIdx < 0 || d.latestIdx >= (int)d.filteredIndices.size()) return;

    auto& latestFile = d.allFiles[d.filteredIndices[d.latestIdx]];
    EnsureLoaded(latestFile);

    bool hasOld = (d.selectedFileIdx >= 0 && d.selectedFileIdx < (int)d.filteredIndices.size());
    std::vector<CsvMember>* oldMembers = nullptr;
    if (hasOld) {
        auto& oldFile = d.allFiles[d.filteredIndices[d.selectedFileIdx]];
        EnsureLoaded(oldFile);
        oldMembers = &oldFile.members;
    }

    // 最新メンバーを名前でマップ
    std::map<std::wstring, const CsvMember*> latestMap;
    for (auto& m : latestFile.members) latestMap[m.name] = &m;

    // 旧メンバーを名前でマップ
    std::map<std::wstring, const CsvMember*> oldMap;
    if (oldMembers) {
        for (auto& m : *oldMembers) oldMap[m.name] = &m;
    }

    // 最新メンバー行を追加
    for (auto& m : latestFile.members) {
        CompareRow r;
        r.name = m.name;
        r.level = std::to_wstring(m.level);
        r.job = m.job;
        r.rank = m.rank;

        if (hasOld) {
            auto it = oldMap.find(m.name);
            if (it == oldMap.end()) {
                r.oldLevel = L"加入";
                r.status = 1;
            } else {
                r.oldLevel = std::to_wstring(it->second->level);
                if (it->second->level != m.level) {
                    r.status = 3; // レベル変化
                }
            }
        }
        d.rows.push_back(std::move(r));
    }

    // 旧にいて最新にいないメンバー → 脱退
    if (hasOld) {
        for (auto& m : *oldMembers) {
            if (latestMap.find(m.name) == latestMap.end()) {
                CompareRow r;
                r.name = m.name;
                r.level = L"--";
                r.job = m.job;
                r.rank = m.rank;
                r.oldLevel = L"脱退";
                r.status = 2;
                d.rows.push_back(std::move(r));
            }
        }
    }

    // visibleRows を構築
    for (int i = 0; i < (int)d.rows.size(); i++) {
        auto& r = d.rows[i];
        // 職業フィルタ
        if (!d.jobFilter.empty() && r.job != d.jobFilter) continue;
        // 変化フィルタ
        if (d.showChangedOnly && r.status == 0) continue;
        if (d.showUnchangedOnly && r.status != 0) continue;
        d.visibleRows.push_back(i);
    }

    // レベルソート適用
    if (d.lvSortOrder != 0) {
        std::sort(d.visibleRows.begin(), d.visibleRows.end(),
            [&](int a, int b) {
                // "--" (脱退) は常に末尾
                bool aNum = (d.rows[a].level != L"--");
                bool bNum = (d.rows[b].level != L"--");
                if (!aNum && !bNum) return false;
                if (!aNum) return false;
                if (!bNum) return true;
                int la = 0, lb = 0;
                try { la = std::stoi(d.rows[a].level); } catch (...) {}
                try { lb = std::stoi(d.rows[b].level); } catch (...) {}
                return d.lvSortOrder == 1 ? (la < lb) : (la > lb);
            });
    }
}

// ───────────────────────── 情報テキスト構築 ─────────────────────────
static std::wstring BuildInfoText(CompareWndData& d) {
    std::wstring info;
    if (d.latestIdx < 0 || d.latestIdx >= (int)d.filteredIndices.size()) {
        info = L"CSVファイルがありません";
        return info;
    }

    auto& latestFile = d.allFiles[d.filteredIndices[d.latestIdx]];
    EnsureLoaded(latestFile);

    // 1行目: ギルド名と合計人数
    int total = (int)latestFile.members.size();
    info += L"ギルド名: " + d.selectedGuild + L"  合計: " + std::to_wstring(total) + L"名";
    return info;
}

static std::wstring BuildEventLogText(CompareWndData& d) {
    if (d.latestIdx < 0 || d.latestIdx >= (int)d.filteredIndices.size())
        return {};
    if (d.selectedFileIdx < 0)
        return L"ファイル一覧から比較対象の過去ファイルを選択してください";

    auto& latestFile = d.allFiles[d.filteredIndices[d.latestIdx]];
    auto& oldFile = d.allFiles[d.filteredIndices[d.selectedFileIdx]];
    std::wstring text;
    time_t tNew = std::mktime(&latestFile.parsedTime);
    time_t tOld = std::mktime(&const_cast<std::tm&>(oldFile.parsedTime));
    if (tNew != -1 && tOld != -1) {
        int days = (int)((tNew - tOld) / 86400);
        text = std::to_wstring(days) + L"日差";
    }
    int joined = 0, left = 0, changed = 0;
    for (auto& r : d.rows) {
        if (r.status == 1) joined++;
        else if (r.status == 2) left++;
        else if (r.status == 3) changed++;
    }
    text += L"  加入:" + std::to_wstring(joined)
          + L" 脱退:" + std::to_wstring(left)
          + L" Lv変化:" + std::to_wstring(changed);
    return text;
}

// ───────────────────────── UI更新 ─────────────────────────
static void RefreshFileList(CompareWndData& d) {
    SendMessageW(d.hFileList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < (int)d.filteredIndices.size(); i++) {
        auto& cf = d.allFiles[d.filteredIndices[i]];
        // 表示: YYYY/MM/DD HH:MM
        std::wstring display;
        if (cf.dateStr.size() >= 15) {
            display = cf.dateStr.substr(0, 4) + L"/" + cf.dateStr.substr(4, 2)
                    + L"/" + cf.dateStr.substr(6, 2) + L" "
                    + cf.dateStr.substr(9, 2) + L":" + cf.dateStr.substr(11, 2);
        } else {
            display = cf.dateStr;
        }
        if (i == 0) display += L" (最新)";
        SendMessageW(d.hFileList, LB_ADDSTRING, 0, (LPARAM)display.c_str());
    }
}

static void RefreshListView(CompareWndData& d) {
    ListView_SetItemCount(d.hListView, (int)d.visibleRows.size());
    InvalidateRect(d.hListView, nullptr, TRUE);
}

// Lv列ヘッダテキスト更新
static void RefreshLvColumnHeader(CompareWndData& d) {
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT;
    wchar_t buf[16];
    if (d.lvSortOrder == 1)
        wcscpy_s(buf, L"Lv \u25b2");
    else if (d.lvSortOrder == 2)
        wcscpy_s(buf, L"Lv \u25bc");
    else
        wcscpy_s(buf, L"Lv \u25bd");
    col.pszText = buf;
    ListView_SetColumn(d.hListView, 3, &col);  // 列3列目 = Lv
}

static void RefreshInfo(CompareWndData& d) {
    auto text = BuildInfoText(d);
    SetWindowTextW(d.hInfo, text.c_str());
}

// 職業ボタンのテキストと表示を更新
static void RefreshJobButtons(CompareWndData& d) {
    std::map<std::wstring, int> jobCounts;
    if (d.latestIdx >= 0 && d.latestIdx < (int)d.filteredIndices.size()) {
        auto& lf = d.allFiles[d.filteredIndices[d.latestIdx]];
        EnsureLoaded(lf);
        for (auto& m : lf.members) jobCounts[m.job]++;
    }
    for (int i = 0; i < kJobCount; i++) {
        auto it = jobCounts.find(kJobs[i].fullName);
        int cnt = (it != jobCounts.end()) ? it->second : 0;
        if (cnt > 0) {
            std::wstring label = kJobs[i].shortName;
            label += L":";
            label += std::to_wstring(cnt);
            SetWindowTextW(d.hJobBtns[i], label.c_str());
            ShowWindow(d.hJobBtns[i], SW_SHOW);
        } else {
            ShowWindow(d.hJobBtns[i], SW_HIDE);
        }
        // アクティブフィルタの場合は押し込み表示
        bool active = (!d.jobFilter.empty() && d.jobFilter == kJobs[i].fullName);
        SendMessageW(d.hJobBtns[i], BM_SETCHECK, active ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    // ボタンレイアウトを直接実行 (WM_SIZE再送信は再入問題を起こすため避ける)
    HWND hParent = GetParent(d.hJobBtns[0]);
    if (hParent) {
        RECT rc;
        GetClientRect(hParent, &rc);
        int cw = rc.right;
        constexpr int btnW = 64, btnH = 22, gap = 2;
        int bx = 4, by = kInfoH;
        for (int i = 0; i < kJobCount; i++) {
            if (!IsWindowVisible(d.hJobBtns[i])) continue;
            if (bx + btnW > cw - 4) { bx = 4; by += btnH + gap; }
            MoveWindow(d.hJobBtns[i], bx, by, btnW, btnH, TRUE);
            bx += btnW + gap;
        }
    }
}

static void RefreshEventLog(CompareWndData& d) {
    auto text = BuildEventLogText(d);
    SetWindowTextW(d.hEventLog, text.c_str());
}

static void RefreshAll(CompareWndData& d) {
    BuildCompareRows(d);
    RefreshInfo(d);
    RefreshJobButtons(d);
    RefreshEventLog(d);
    RefreshLvColumnHeader(d);
    RefreshListView(d);
}

// ───────────────────────── ギルド選択変更 ─────────────────────────
static void OnGuildChanged(CompareWndData& d) {
    int sel = (int)SendMessageW(d.hGuildCombo, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)d.guildNames.size()) return;
    d.selectedGuild = d.guildNames[sel];
    FilterByGuild(d);
    RefreshFileList(d);
    RefreshAll(d);
}

// ───────────────────────── ファイル選択変更 ─────────────────────────
static void OnFileSelected(CompareWndData& d) {
    int sel = (int)SendMessageW(d.hFileList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)d.filteredIndices.size()) return;
    if (sel == 0) {
        d.selectedFileIdx = -1; // 最新を選んだ = 比較なし
    } else {
        d.selectedFileIdx = sel;
    }
    RefreshAll(d);
}

// ───────────────────────── CSV保存 ─────────────────────────
static void OnSaveCsv(CompareWndData& d) {
    if (d.visibleRows.empty()) return;

    wchar_t fileBuf[MAX_PATH] = L"比較結果.csv";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = s_hCompareWnd;
    ofn.lpstrFilter = L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) return;

    std::ofstream ofs(fileBuf, std::ios::binary);
    if (!ofs) return;

    // UTF-8 BOM
    ofs.write("\xEF\xBB\xBF", 3);

    // ヘッダ
    bool hasOld = (d.selectedFileIdx >= 0);
    std::string header = "ギルド員名,職業,役職,レベル";
    if (hasOld) header += ",旧レベル,状態";
    header += "\r\n";

    // wstring → UTF-8 変換ヘルパ
    auto toUtf8 = [](const std::wstring& ws) -> std::string {
        if (ws.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string s(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), len, nullptr, nullptr);
        return s;
    };

    ofs << header;
    for (int vi : d.visibleRows) {
        auto& r = d.rows[vi];
        std::string line = toUtf8(r.name) + "," + toUtf8(r.job) + ","
                         + toUtf8(r.rank) + "," + toUtf8(r.level);
        if (hasOld) {
            line += "," + toUtf8(r.oldLevel);
            std::string st;
            switch (r.status) {
            case 1: st = toUtf8(L"加入"); break;
            case 2: st = toUtf8(L"脱退"); break;
            case 3: st = toUtf8(L"変化"); break;
            default: break;
            }
            line += "," + st;
        }
        line += "\r\n";
        ofs << line;
    }
}

// ───────────────────────── WndProc ─────────────────────────
static LRESULT CALLBACK CompareWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* d = reinterpret_cast<CompareWndData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE:
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        d = reinterpret_cast<CompareWndData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)d);

        // ギルド選択行
        d->hGuildLabel = CreateWindowExW(0, L"STATIC", L"ギルド:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            4, 6, 50, 18, hwnd, (HMENU)(UINT_PTR)IDC_CMP_GUILDLABEL, d->hInst, nullptr);
        d->hGuildCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            56, 3, 200, 200, hwnd, (HMENU)(UINT_PTR)IDC_CMP_GUILDCOMBO, d->hInst, nullptr);

        // イベントログ (ギルド選択行の右側)
        d->hEventLog = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            260, 4, 300, 20, hwnd, (HMENU)(UINT_PTR)IDC_CMP_EVENTLOG, d->hInst, nullptr);

        // 上部情報パネル
        d->hInfo = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            4, kGuildRowH + 2, 600, kInfoH - kGuildRowH - 4, hwnd, (HMENU)(UINT_PTR)IDC_CMP_INFO, d->hInst, nullptr);

        // 職業フィルタボタン (kInfoH の下に配置、2行分)
        for (int i = 0; i < kJobCount; i++) {
            d->hJobBtns[i] = CreateWindowExW(0, L"BUTTON", L"",
                WS_CHILD | BS_PUSHBUTTON | BS_CENTER,
                0, 0, 60, 22, hwnd, (HMENU)(INT_PTR)(IDC_CMP_JOB_BASE + i), d->hInst, nullptr);
            if (d->hFont) SendMessageW(d->hJobBtns[i], WM_SETFONT, (WPARAM)d->hFont, TRUE);
        }

        // ツールチップ (職業ボタン用)
        d->hTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            0, 0, 0, 0, hwnd, nullptr, d->hInst, nullptr);
        if (d->hTooltip) {
            for (int i = 0; i < kJobCount; i++) {
                TTTOOLINFOW ti{};
                ti.cbSize = TTTOOLINFOW_V1_SIZE;
                ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                ti.hwnd = hwnd;
                ti.uId = (UINT_PTR)d->hJobBtns[i];
                ti.lpszText = const_cast<wchar_t*>(L"    再度押せば全表示に戻ります");
                SendMessageW(d->hTooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            }
        }

        // 左ファイルリストヘッダ
        d->hFileListLabel = CreateWindowExW(0, L"STATIC", L"ファイル一覧",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            0, kTopH, kFileListW, 18, hwnd, (HMENU)(UINT_PTR)IDC_CMP_FILELISTLABEL, d->hInst, nullptr);

        // 左ファイルリスト
        d->hFileList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
            0, kInfoH, kFileListW, 300, hwnd, (HMENU)(UINT_PTR)IDC_CMP_FILELIST, d->hInst, nullptr);

        // 右比較ListView (仮想リスト)
        d->hListView = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            kFileListW, kInfoH, 500, 300, hwnd, (HMENU)(UINT_PTR)IDC_CMP_LISTVIEW, d->hInst, nullptr);
        ListView_SetExtendedListViewStyle(d->hListView,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        // ListView 列追加
        {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;
            col.pszText = const_cast<wchar_t*>(L"ギルド員名"); col.cx = 140;
            ListView_InsertColumn(d->hListView, 0, &col);
            col.pszText = const_cast<wchar_t*>(L"職業"); col.cx = 100;
            ListView_InsertColumn(d->hListView, 1, &col);
            col.pszText = const_cast<wchar_t*>(L"役職"); col.cx = 70;
            ListView_InsertColumn(d->hListView, 2, &col);
            col.pszText = const_cast<wchar_t*>(L"Lv \u25bd"); col.cx = 50;
            col.fmt = LVCFMT_RIGHT;
            ListView_InsertColumn(d->hListView, 3, &col);
            col.pszText = const_cast<wchar_t*>(L"旧Lv"); col.cx = 50;
            ListView_InsertColumn(d->hListView, 4, &col);
        }

        // 下部: 変化ありのみチェック + CSV保存ボタン
        d->hChangedOnly = CreateWindowExW(0, L"BUTTON", L"変化ありのみ",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            8, 0, 120, 22, hwnd, (HMENU)(UINT_PTR)IDC_CMP_CHANGEDONLY, d->hInst, nullptr);
        d->hUnchangedOnly = CreateWindowExW(0, L"BUTTON", L"変化無しのみ",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            130, 0, 120, 22, hwnd, (HMENU)(UINT_PTR)IDC_CMP_UNCHANGEDONLY, d->hInst, nullptr);
        d->hSaveBtn = CreateWindowExW(0, L"BUTTON", L"CSV保存",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 80, 24, hwnd, (HMENU)(UINT_PTR)IDC_CMP_SAVE, d->hInst, nullptr);

        // フォント設定
        if (d->hFont) {
            SendMessageW(d->hGuildLabel, WM_SETFONT, (WPARAM)d->hFont, TRUE);
            SendMessageW(d->hGuildCombo, WM_SETFONT, (WPARAM)d->hFont, TRUE);
            SendMessageW(d->hInfo, WM_SETFONT, (WPARAM)d->hFont, TRUE);
            SendMessageW(d->hFileListLabel, WM_SETFONT, (WPARAM)d->hFont, TRUE);
            SendMessageW(d->hFileList, WM_SETFONT, (WPARAM)d->hFont, TRUE);
            SendMessageW(d->hListView, WM_SETFONT, (WPARAM)d->hFont, TRUE);
            SendMessageW(d->hEventLog, WM_SETFONT, (WPARAM)d->hFont, TRUE);
            SendMessageW(d->hChangedOnly, WM_SETFONT, (WPARAM)d->hFont, TRUE);
            SendMessageW(d->hUnchangedOnly, WM_SETFONT, (WPARAM)d->hFont, TRUE);
            SendMessageW(d->hSaveBtn, WM_SETFONT, (WPARAM)d->hFont, TRUE);
            // 職業ボタンは上のループで設定済み
        }

        // データ初期化
        ScanGuildFolder(*d);

        // ギルドComboBox にアイテム追加
        for (auto& g : d->guildNames) {
            SendMessageW(d->hGuildCombo, CB_ADDSTRING, 0, (LPARAM)g.c_str());
        }

        // 自動選択: ギルド名が1つなら自動選択、複数なら最初を選択
        if (!d->guildNames.empty()) {
            SendMessageW(d->hGuildCombo, CB_SETCURSEL, 0, 0);
            d->selectedGuild = d->guildNames[0];
            FilterByGuild(*d);
            RefreshFileList(*d);
            RefreshAll(*d);
        }
        return 0;
    }

    case WM_SIZE:
    {
        if (!d) break;
        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right, ch = rc.bottom;
        int bodyTop = kTopH;
        int bodyH = ch - kTopH - kBottomH;
        if (bodyH < 0) bodyH = 0;

        // ギルド選択行
        MoveWindow(d->hGuildLabel, 4, 6, 50, 18, FALSE);
        MoveWindow(d->hGuildCombo, 56, 3, 200, 200, FALSE);
        MoveWindow(d->hEventLog, 260, 4, cw - 264, 20, FALSE);

        // 情報パネル (ギルド行の下)
        MoveWindow(d->hInfo, 4, kGuildRowH + 2, cw - 8, kInfoH - kGuildRowH - 4, FALSE);

        // 職業ボタンのレイアウト (可視ボタンのみ左詰めで並べる)
        {
            constexpr int btnW = 64, btnH = 22, gap = 2;
            int bx = 4, by = kInfoH;
            for (int i = 0; i < kJobCount; i++) {
                if (!IsWindowVisible(d->hJobBtns[i])) continue;
                if (bx + btnW > cw - 4) { bx = 4; by += btnH + gap; }
                MoveWindow(d->hJobBtns[i], bx, by, btnW, btnH, TRUE);
                bx += btnW + gap;
            }
        }

        // 左ファイルリスト
        constexpr int labelH = 18;
        MoveWindow(d->hFileListLabel, 0, bodyTop, kFileListW, labelH, FALSE);
        MoveWindow(d->hFileList, 0, bodyTop + labelH, kFileListW, bodyH - labelH, FALSE);

        // 右ListView
        MoveWindow(d->hListView, kFileListW, bodyTop, cw - kFileListW, bodyH, FALSE);

        // 下部パネル
        MoveWindow(d->hChangedOnly, 8, ch - kBottomH + 5, 120, 22, FALSE);
        MoveWindow(d->hUnchangedOnly, 130, ch - kBottomH + 5, 120, 22, FALSE);
        MoveWindow(d->hSaveBtn, cw - 88, ch - kBottomH + 4, 80, 24, FALSE);

        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_COMMAND:
    {
        if (!d) break;
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_CMP_GUILDCOMBO && code == CBN_SELCHANGE) {
            OnGuildChanged(*d);
        }
        if (id == IDC_CMP_FILELIST && code == LBN_SELCHANGE) {
            OnFileSelected(*d);
        }
        if (id == IDC_CMP_CHANGEDONLY && code == BN_CLICKED) {
            d->showChangedOnly = (SendMessageW(d->hChangedOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (d->showChangedOnly) {
                d->showUnchangedOnly = false;
                SendMessageW(d->hUnchangedOnly, BM_SETCHECK, BST_UNCHECKED, 0);
            }
            RefreshAll(*d);
        }
        if (id == IDC_CMP_UNCHANGEDONLY && code == BN_CLICKED) {
            d->showUnchangedOnly = (SendMessageW(d->hUnchangedOnly, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (d->showUnchangedOnly) {
                d->showChangedOnly = false;
                SendMessageW(d->hChangedOnly, BM_SETCHECK, BST_UNCHECKED, 0);
            }
            RefreshAll(*d);
        }
        if (id == IDC_CMP_SAVE && code == BN_CLICKED) {
            OnSaveCsv(*d);
        }
        // 職業フィルタボタン
        if (id >= IDC_CMP_JOB_BASE && id < IDC_CMP_JOB_BASE + kJobCount && code == BN_CLICKED) {
            int ji = id - IDC_CMP_JOB_BASE;
            if (d->jobFilter == kJobs[ji].fullName) {
                d->jobFilter.clear(); // トグル解除
            } else {
                d->jobFilter = kJobs[ji].fullName;
            }
            RefreshAll(*d);
        }
        return 0;
    }

    case WM_NOTIFY:
    {
        if (!d) break;
        auto* nm = reinterpret_cast<NMHDR*>(lParam);
        if (nm->hwndFrom == d->hListView && nm->code == LVN_GETDISPINFOW) {
            auto* di = reinterpret_cast<NMLVDISPINFOW*>(lParam);
            int visIdx = di->item.iItem;
            if (visIdx < 0 || visIdx >= (int)d->visibleRows.size()) return 0;
            int rowIdx = d->visibleRows[visIdx];
            auto& r = d->rows[rowIdx];

            if (di->item.mask & LVIF_TEXT) {
                const std::wstring* src = nullptr;
                switch (di->item.iSubItem) {
                case 0: src = &r.name; break;
                case 1: src = &r.job; break;
                case 2: src = &r.rank; break;
                case 3: src = &r.level; break;
                case 4: src = &r.oldLevel; break;
                }
                if (src) wcsncpy_s(di->item.pszText, di->item.cchTextMax, src->c_str(), _TRUNCATE);
            }
            return 0;
        }

        // Lv列ヘッダクリックでソート切替
        if (nm->hwndFrom == d->hListView && nm->code == LVN_COLUMNCLICK) {
            auto* nmlv = reinterpret_cast<NMLISTVIEW*>(lParam);
            if (nmlv->iSubItem == 3) { // Lv列
                // 0→降順→2→昇順→1→なし→0
                d->lvSortOrder = (d->lvSortOrder == 0) ? 2 : (d->lvSortOrder == 2) ? 1 : 0;
                BuildCompareRows(*d);
                RefreshLvColumnHeader(*d);
                RefreshListView(*d);
            }
            return 0;
        }

        // カスタムドロー: 状態に応じた色分け
        if (nm->hwndFrom == d->hListView && nm->code == NM_CUSTOMDRAW) {
            auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT:
            {
                int visIdx = (int)cd->nmcd.dwItemSpec;
                if (visIdx >= 0 && visIdx < (int)d->visibleRows.size()) {
                    int rowIdx = d->visibleRows[visIdx];
                    auto& r = d->rows[rowIdx];
                    switch (r.status) {
                    case 1: // 加入 → 青背景
                        cd->clrTextBk = RGB(220, 235, 255);
                        cd->clrText = RGB(0, 0, 180);
                        break;
                    case 2: // 脱退 → ピンク背景
                        cd->clrTextBk = RGB(255, 225, 225);
                        cd->clrText = RGB(180, 0, 0);
                        break;
                    case 3: // レベル変化 → 薄黄背景
                        cd->clrTextBk = RGB(255, 255, 210);
                        cd->clrText = RGB(0, 0, 0);
                        break;
                    }
                }
                return CDRF_NEWFONT;
            }
            }
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (d) {
            delete d;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        s_hCompareWnd = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ───────────────────────── 公開関数 ─────────────────────────
void ShowGuildCompareWindow(HINSTANCE hInst, HWND hParent,
                            const std::wstring& guildFolder, HFONT hFont) {
    // 既に開いていれば前面に出す
    if (s_hCompareWnd && IsWindow(s_hCompareWnd)) {
        SetForegroundWindow(s_hCompareWnd);
        return;
    }

    // ウィンドウクラス登録 (初回のみ)
    if (!s_classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = CompareWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = kCmpClass;
        wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(1)); // アプリアイコン
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    // データ確保
    auto* d = new CompareWndData();
    d->hFont = hFont;
    d->guildFolder = guildFolder;
    d->hInst = hInst;

    // ウィンドウ作成
    s_hCompareWnd = CreateWindowExW(0, kCmpClass, L"ギルド員比較",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 560,
        hParent, nullptr, hInst, d);

    CenterWindowOnParent(s_hCompareWnd, hParent);
    ShowWindow(s_hCompareWnd, SW_SHOW);
    UpdateWindow(s_hCompareWnd);
}
