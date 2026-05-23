#include "roten_processor.h"
#include "item_dat_loader.h"
#include "resource.h"
#include <windows.h>
#include <objidl.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <thread>

namespace fs = std::filesystem;

// Restore SJIS-only decoder
static std::wstring SjisToWide(const uint8_t* data, int len) {
    if (!data || len <= 0) return std::wstring();
    int wlen = MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(data), len, nullptr, 0);
    if (wlen <= 0) return std::wstring();
    std::wstring ws(wlen, L'\0');
    MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(data), len, ws.data(), wlen);
    while (!ws.empty() && (ws.back() == L' ' || ws.back() == L'\0')) ws.pop_back();
    return ws;
}

// wstring → Shift-JIS bytes
static std::string WideToSjis(const std::wstring& ws) {
    if (ws.empty()) return {};
    int n = WideCharToMultiByte(932, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n, '\0');
    WideCharToMultiByte(932, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// HEX文字列化 (重複検出用)
static std::string ToHexString(const uint8_t* data, int len) {
    std::string hex;
    hex.reserve(len * 2);
    for (int i = 0; i < len; i++) {
        char buf[3];
        std::snprintf(buf, 3, "%02x", data[i]);
        hex += buf;
    }
    return hex;
}

RotenProcessor::RotenProcessor(ItemDatLoader& itemDatLoader, int maxLinesPerFile)
    : itemDatLoader_(itemDatLoader)
    , maxLinesPerFile_(maxLinesPerFile)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    auto pos = exeDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) exeDir = exeDir.substr(0, pos);

    logDir_ = exeDir + L"\\ログ\\露店売買";
    try { fs::create_directories(logDir_); } catch (...) {}
}

void RotenProcessor::ResetState() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    precedingReceived_ = false;
    captureCooldown_ = false;
    lastPackets_.clear();
}

bool RotenProcessor::IsDuplicate(const uint8_t* data, int len) {
    auto hex = ToHexString(data, len);
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(stateMutex_);

    auto it = lastPackets_.find(hex);
    if (it != lastPackets_.end()) {
        auto elapsed = std::chrono::duration<double>(now - it->second).count();
        if (elapsed < DUPLICATE_WINDOW_SEC) return true;
    }
    lastPackets_[hex] = now;
    return false;
}

void RotenProcessor::CleanupOldEntries() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto it = lastPackets_.begin(); it != lastPackets_.end(); ) {
        auto elapsed = std::chrono::duration<double>(now - it->second).count();
        if (elapsed > CLEANUP_THRESHOLD_SEC)
            it = lastPackets_.erase(it);
        else
            ++it;
    }
}

void RotenProcessor::ProcessPacket(const uint8_t* data, int len, bool isIncoming, int /*srcPort*/, int /*dstPort*/) {
    try {
        if (!data || len < 43) return;

        uint8_t cmd0 = data[2], cmd1 = data[3];

        // Preceding packet: PRE_COMMAND_ID from client→server (isIncoming==false)
        if (cmd0 == PRE_COMMAND_ID[0] && cmd1 == PRE_COMMAND_ID[1] && !isIncoming) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastPrecedingTime_ = std::chrono::steady_clock::now();
            precedingReceived_ = true;
            return;
        }

        // sub command at offset 14..15 (little-endian)
        if (len < 16) return;
        uint16_t subCommand = data[14] | (data[15] << 8);

        // Duplicate check
        if (IsDuplicate(data, len)) return;
        CleanupOldEntries();

        // Only process if main command matches
        if (cmd0 != COMMAND_ID[0] || cmd1 != COMMAND_ID[1]) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            precedingReceived_ = false;
            return;
        }

        SYSTEMTIME st;
        GetLocalTime(&st);

        if (subCommand == SUB_COMMAND_SALE) {
            std::wstring purchaserName;
            uint32_t salesAmount = 0;
            uint16_t slotNumber = 0;
            uint8_t currencyType = 0;

            if (TryParseSalePacket(data, len, purchaserName, salesAmount, slotNumber, currencyType)) {
                std::wstring currencyName = (currencyType == 0) ? L"ゴールド" :
                                            (currencyType == 1) ? L"インゴット" : L"不明";
                auto formatted = FormatJapaneseAmount(salesAmount);
                int displaySlot = slotNumber + 1;
                std::wstring msg = std::to_wstring(displaySlot) + L"番目の商品を " +
                                   purchaserName + L"さんへ " + formatted + L" " + currencyName + L"で販売しました";
                auto line = FormatLogLine(st.wMonth, st.wDay, st.wHour, st.wMinute, msg);
                AppendLineToLog(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, line);
            }
        } else if (subCommand == SUB_COMMAND_PURCHASE) {
            std::wstring ownerName;
            uint32_t price = 0;
            uint16_t itemSerial = 0;
            uint8_t currencyType = 0;
            std::wstring optionString;

            if (TryParsePurchasePacket(data, len, ownerName, price, itemSerial, currencyType, optionString)) {
                std::wstring itemName = itemDatLoader_.GetItemName(itemSerial);

                std::wstring purchaseMsg;
                if (currencyType == 0) {
                    purchaseMsg = ownerName + L"さんから " + itemName + optionString +
                                  L" を " + FormatJapaneseAmount(price) + L" ゴールドで購入しました";
                } else if (currencyType == 1) {
                    // インゴット: 桁区切り
                    std::wstring priceStr = std::to_wstring(price);
                    purchaseMsg = ownerName + L"さんから " + itemName + optionString +
                                  L" を " + priceStr + L" インゴットで購入しました";
                } else {
                    std::wstring priceStr = std::to_wstring(price);
                    purchaseMsg = ownerName + L"さんから " + itemName + optionString +
                                  L" を " + priceStr + L" (不明な通貨:" + std::to_wstring(currencyType) + L")で購入しました";
                }
                auto line = FormatLogLine(st.wMonth, st.wDay, st.wHour, st.wMinute, purchaseMsg);
                AppendLineToLog(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, line);
            }
        } else if (subCommand == SUB_COMMAND_START_ROTEN) {
            // Only trigger if preceding packet was received recently
            bool shouldStartCapture = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                if (precedingReceived_) {
                    auto elapsed = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - lastPrecedingTime_).count();
                    if (elapsed <= PRECEDING_TIMEOUT_SEC && !captureCooldown_) {
                        captureCooldown_ = true;
                        shouldStartCapture = true;
                    }
                }
                precedingReceived_ = false;
            }

            if (shouldStartCapture) {
                // Extract roten message text
                std::wstring rotenMessage;
                if (len > 28) {
                    int textStart = 28;
                    int nullIdx = -1;
                    for (int i = textStart; i < len; i++) {
                        if (data[i] == 0) { nullIdx = i; break; }
                    }
                    if (nullIdx > textStart) {
                        rotenMessage = SjisToWide(data + textStart, nullIdx - textStart);
                    }
                }

                // C# 仕様: 500ms遅延 → キャプチャ → ログ書き込み → GUI通知
                std::wstring pendingMsg = rotenMessage;
                int y = st.wYear, mo = st.wMonth, d = st.wDay, h = st.wHour, mi = st.wMinute;
                std::thread([this, pendingMsg, y, mo, d, h, mi]() {
                    Sleep(300);
                    bool ok = CaptureRotenStartArea();

                    // GUI通知: 画像キャプチャ成功メッセージ（AppendLineToLogより先に表示）
                    if (ok && logCallback_) {
                        logCallback_({L"", L"露店開始画像をキャプチャしました"});
                    }

                    // ログ書き込み + GUI通知（AppendLineToLog内でlogCallback_も呼ばれる）
                    std::wstring logMsg = L"露店開始 [" + pendingMsg + L"]";
                    SYSTEMTIME stNow; GetLocalTime(&stNow);
                    auto line = FormatLogLine(stNow.wMonth, stNow.wDay, stNow.wHour, stNow.wMinute, logMsg);
                    AppendLineToLog(stNow.wYear, stNow.wMonth, stNow.wDay, stNow.wHour, stNow.wMinute, line);

                    // ビューア更新（UIスレッドへ通知）
                    if (ok && captureCallback_) {
                        captureCallback_();
                    }

                    std::lock_guard<std::mutex> lock(stateMutex_);
                    captureCooldown_ = false;
                }).detach();
            }
        }
    } catch (...) {
        // swallow
    }
}

bool RotenProcessor::TryParseSalePacket(const uint8_t* data, int len,
                                         std::wstring& purchaserName, uint32_t& salesAmount,
                                         uint16_t& slotNumber, uint8_t& currencyType) {
    try {
        int textStart = 18;
        // Find null terminator
        int nullIdx = -1;
        for (int i = textStart; i < len; i++) {
            if (data[i] == 0) { nullIdx = i; break; }
        }
        if (nullIdx <= textStart) return false;

        purchaserName = SjisToWide(data + textStart, nullIdx - textStart);
        if (purchaserName.empty()) return false;

        int salesAmountStart = 40;
        if (len < salesAmountStart + 4) return false;
        salesAmount = data[salesAmountStart] | (data[salesAmountStart+1] << 8) |
                      (data[salesAmountStart+2] << 16) | (data[salesAmountStart+3] << 24);

        int currencyStart = salesAmountStart + 4; // 44
        if (len < currencyStart + 1) return false;
        currencyType = data[currencyStart];

        int slotStart = 36;
        if (len < slotStart + 2) return false;
        slotNumber = data[slotStart] | (data[slotStart+1] << 8);

        return true;
    } catch (...) {
        return false;
    }
}

bool RotenProcessor::TryParsePurchasePacket(const uint8_t* data, int len,
                                             std::wstring& ownerName, uint32_t& price,
                                             uint16_t& itemSerial, uint8_t& currencyType,
                                             std::wstring& optionString) {
    try {
        if (len < 28) return false;
        itemSerial = data[26] | (data[27] << 8);

        // Option serials at offsets 32, 36, 40
        std::vector<uint16_t> optionSerials;
        for (int offset : {32, 36, 40}) {
            if (len >= offset + 2)
                optionSerials.push_back(data[offset] | (data[offset+1] << 8));
            else
                optionSerials.push_back(0);
        }

        std::wstring opts;
        for (auto s : optionSerials) {
            auto name = itemDatLoader_.GetOptionName(s);
            if (!name.empty()) opts += name;
        }
        optionString = opts;

        if (len < 56) return false;
        price = data[52] | (data[53] << 8) | (data[54] << 16) | (data[55] << 24);

        if (len < 57) return false;
        currencyType = data[56];

        int textStart = 57;
        if (len <= textStart) return false;
        int nullIdx = -1;
        for (int i = textStart; i < len; i++) {
            if (data[i] == 0) { nullIdx = i; break; }
        }
        if (nullIdx <= textStart) return false;
        ownerName = SjisToWide(data + textStart, nullIdx - textStart);
        if (ownerName.empty()) return false;

        return true;
    } catch (...) {
        return false;
    }
}

// ═══════════════════════════════════════════════════════════════════
// EXEディレクトリ取得
// ═══════════════════════════════════════════════════════════════════
static std::wstring GetExeDirectory() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    auto p = s.find_last_of(L"\\/");
    return (p != std::wstring::npos) ? s.substr(0, p) : s;
}

// ═══════════════════════════════════════════════════════════════════
// BMP → グレースケール変換
// ═══════════════════════════════════════════════════════════════════
GrayImage RotenProcessor::BmpBytesToGray(const uint8_t* data, int dataSize) {
    GrayImage img;
    if (dataSize < (int)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER))) return img;

    auto* fh = reinterpret_cast<const BITMAPFILEHEADER*>(data);
    auto* ih = reinterpret_cast<const BITMAPINFOHEADER*>(data + sizeof(BITMAPFILEHEADER));

    if (fh->bfType != 0x4D42) return img;
    int w = ih->biWidth;
    int h = std::abs(ih->biHeight);
    bool topDown = (ih->biHeight < 0);
    int bpp = ih->biBitCount;
    if (bpp != 24 && bpp != 32) return img;

    int bytesPerPixel = bpp / 8;
    int rowStride = ((w * bytesPerPixel + 3) & ~3);
    const uint8_t* pixels = data + fh->bfOffBits;

    img.width = w;
    img.height = h;
    img.data.resize(w * h);

    for (int y = 0; y < h; y++) {
        int srcRow = topDown ? y : (h - 1 - y);
        const uint8_t* row = pixels + srcRow * rowStride;
        for (int x = 0; x < w; x++) {
            uint8_t b = row[x * bytesPerPixel];
            uint8_t g = row[x * bytesPerPixel + 1];
            uint8_t r = row[x * bytesPerPixel + 2];
            img.data[y * w + x] = static_cast<uint8_t>(0.299 * r + 0.587 * g + 0.114 * b);
        }
    }
    return img;
}

// ═══════════════════════════════════════════════════════════════════
// リソースからテンプレート画像を読み込み
// ═══════════════════════════════════════════════════════════════════
GrayImage RotenProcessor::LoadTemplateFromResource(int resourceId) {
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) return {};
    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return {};
    auto* ptr = static_cast<const uint8_t*>(LockResource(hData));
    int sz = static_cast<int>(SizeofResource(nullptr, hRes));
    if (!ptr || sz <= 0) return {};
    return BmpBytesToGray(ptr, sz);
}

// ═══════════════════════════════════════════════════════════════════
// ウィンドウのスクリーンキャプチャ → グレースケール
// ═══════════════════════════════════════════════════════════════════
GrayImage RotenProcessor::CaptureWindowGray(HWND hwnd, RECT& outRect) {
    GrayImage img;
    if (!GetWindowRect(hwnd, &outRect)) return img;

    int w = outRect.right - outRect.left;
    int h = outRect.bottom - outRect.top;
    if (w <= 0 || h <= 0) return img;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, w, h);
    HGDIOBJ hOld = SelectObject(hdcMem, hBmp);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, outRect.left, outRect.top, SRCCOPY);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    int rowSize = ((w * 3 + 3) & ~3);
    bi.biSizeImage = rowSize * h;

    std::vector<uint8_t> pixels(bi.biSizeImage);
    GetDIBits(hdcMem, hBmp, 0, h, pixels.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    // BGR24 → gray
    img.width = w;
    img.height = h;
    img.data.resize(w * h);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = pixels.data() + y * rowSize;
        for (int x = 0; x < w; x++) {
            uint8_t b = row[x * 3];
            uint8_t g = row[x * 3 + 1];
            uint8_t r = row[x * 3 + 2];
            img.data[y * w + x] = static_cast<uint8_t>(0.299 * r + 0.587 * g + 0.114 * b);
        }
    }
    return img;
}

// ═══════════════════════════════════════════════════════════════════
// NCC テンプレートマッチング (正規化相互相関 CCoeffNormed相当)
// ═══════════════════════════════════════════════════════════════════
bool RotenProcessor::MatchTemplateNCC(const GrayImage& src, const GrayImage& tpl,
                                       double threshold, int& outX, int& outY, double& outScore,
                                       int searchStartY) {
    if (src.empty() || tpl.empty()) return false;
    int sw = src.width, sh = src.height;
    int tw = tpl.width, th = tpl.height;
    if (tw > sw || th > sh) return false;

    // テンプレート平均・分散
    double tSum = 0;
    for (int i = 0; i < tw * th; i++) tSum += tpl.data[i];
    double tMean = tSum / (tw * th);
    double tVar = 0;
    for (int i = 0; i < tw * th; i++) {
        double d = tpl.data[i] - tMean;
        tVar += d * d;
    }
    double tStd = std::sqrt(tVar);
    if (tStd < 1e-6) return false; // flat template

    double bestScore = -1.0;
    int bestX = 0, bestY = 0;

    int rw = sw - tw + 1;
    int rh = sh - th + 1;
    int tplPixels = tw * th;
    int yStart = (searchStartY > 0 && searchStartY < rh) ? searchStartY : 0;

    for (int y = yStart; y < rh; y++) {
        for (int x = 0; x < rw; x++) {
            // patch mean
            double pSum = 0;
            for (int ty = 0; ty < th; ty++) {
                const uint8_t* sRow = src.data.data() + (y + ty) * sw + x;
                for (int tx = 0; tx < tw; tx++)
                    pSum += sRow[tx];
            }
            double pMean = pSum / tplPixels;

            // NCC numerator and patch variance
            double num = 0, pVar = 0;
            for (int ty = 0; ty < th; ty++) {
                const uint8_t* sRow = src.data.data() + (y + ty) * sw + x;
                const uint8_t* tRow = tpl.data.data() + ty * tw;
                for (int tx = 0; tx < tw; tx++) {
                    double sd = sRow[tx] - pMean;
                    double td = tRow[tx] - tMean;
                    num += sd * td;
                    pVar += sd * sd;
                }
            }
            double pStd = std::sqrt(pVar);
            if (pStd < 1e-6) continue;

            double score = num / (pStd * tStd);
            if (score > bestScore) {
                bestScore = score;
                bestX = x;
                bestY = y;
                // 閾値以上で早期終了（完全一致なら即返す）
                if (bestScore >= threshold) {
                    outX = bestX; outY = bestY; outScore = bestScore;
                    return true;
                }
            }
        }
    }

    outX = bestX;
    outY = bestY;
    outScore = bestScore;
    return bestScore >= threshold;
}

// ═══════════════════════════════════════════════════════════════════
// GDI+ による PNG 保存
// ═══════════════════════════════════════════════════════════════════
bool RotenProcessor::SaveBitmapAsPng(HBITMAP hBmp, int /*w*/, int /*h*/, const std::wstring& path) {
    Gdiplus::Bitmap bmp(hBmp, nullptr);
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;

    // PNG encoder CLSID
    CLSID pngClsid;
    {
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0) return false;
        std::vector<uint8_t> buf(size);
        auto* pEncoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
        Gdiplus::GetImageEncoders(num, size, pEncoders);
        bool found = false;
        for (UINT i = 0; i < num; i++) {
            if (wcscmp(pEncoders[i].MimeType, L"image/png") == 0) {
                pngClsid = pEncoders[i].Clsid;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return bmp.Save(path.c_str(), &pngClsid, nullptr) == Gdiplus::Ok;
}

// ═══════════════════════════════════════════════════════════════════
// CaptureRotenStartArea — テンプレートマッチング版
// ═══════════════════════════════════════════════════════════════════
static void DebugLog(const std::wstring& /*msg*/) {
    // Debug logging disabled to avoid creating capture_debug.log
}

bool RotenProcessor::CaptureRotenStartArea() {
    try {
        DebugLog(L"=== CaptureRotenStartArea START ===");

        // RED STONE ウィンドウを検索 (大文字小文字無視 — C# の OrdinalIgnoreCase と同等)
        HWND gameWnd = nullptr;
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            if (!IsWindowVisible(hwnd)) return TRUE;
            wchar_t title[256];
            GetWindowTextW(hwnd, title, 256);
            // 大文字に変換してから比較
            wchar_t upper[256];
            for (int i = 0; i < 256; i++) {
                upper[i] = towupper(title[i]);
                if (title[i] == L'\0') break;
            }
            if (wcsstr(upper, L"RED STONE") != nullptr) {
                *(HWND*)lParam = hwnd;
                return FALSE;
            }
            return TRUE;
        }, (LPARAM)&gameWnd);
        if (!gameWnd) {
            DebugLog(L"FAIL: RED STONE window not found");
            return false;
        }
        DebugLog(L"OK: Found RED STONE window");

        // フォアグラウンドに
        SetForegroundWindow(gameWnd);
        Sleep(200);

        // ウィンドウキャプチャ → グレースケール
        RECT rect;
        auto gray = CaptureWindowGray(gameWnd, rect);
        if (gray.empty()) {
            DebugLog(L"FAIL: CaptureWindowGray returned empty");
            return false;
        }
        DebugLog(L"OK: CaptureWindowGray " + std::to_wstring(gray.width) + L"x" + std::to_wstring(gray.height));

        // テンプレート読み込み
        auto tplStart = LoadTemplateFromResource(IDR_ROTEN_START);
        auto tplCheck = LoadTemplateFromResource(IDR_ROTEN_CHECK);
        auto tplCheck2 = LoadTemplateFromResource(IDR_ROTEN_CHECK2);

        DebugLog(L"Templates: start=" + std::to_wstring(tplStart.width) + L"x" + std::to_wstring(tplStart.height)
                 + L" check=" + std::to_wstring(tplCheck.width) + L"x" + std::to_wstring(tplCheck.height)
                 + L" check2=" + std::to_wstring(tplCheck2.width) + L"x" + std::to_wstring(tplCheck2.height));

        if (tplStart.empty() || tplCheck.empty()) {
            DebugLog(L"FAIL: template empty (start=" + std::to_wstring(tplStart.empty())
                     + L" check=" + std::to_wstring(tplCheck.empty()) + L")");
            HRSRC hTest = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_ROTEN_START), RT_RCDATA);
            DebugLog(L"FindResourceW(101) = " + std::to_wstring((uintptr_t)hTest));
            return false;
        }

        constexpr double THRESHOLD = 0.75;
        int mx, my; double score;
        // check/check2 は画面下半分にしか存在しないので、中央から下だけ走査（約半分の高速化）
        int halfY = gray.height / 2;

        // check テンプレート → 一致なら既に露店中なのでスキップ
        if (MatchTemplateNCC(gray, tplCheck, THRESHOLD, mx, my, score, halfY)) {
            DebugLog(L"SKIP: tplCheck matched (already in roten) score=" + std::to_wstring(score));
            return false;
        }
        DebugLog(L"OK: tplCheck not matched (score=" + std::to_wstring(score) + L")");

        // check2 テンプレート
        if (!tplCheck2.empty()) {
            if (MatchTemplateNCC(gray, tplCheck2, THRESHOLD, mx, my, score, halfY)) {
                DebugLog(L"SKIP: tplCheck2 matched score=" + std::to_wstring(score));
                return false;
            }
            DebugLog(L"OK: tplCheck2 not matched (score=" + std::to_wstring(score) + L")");
        }

        // start テンプレートマッチ（早期終了あり）
        int matchX, matchY; double matchScore;
        if (!MatchTemplateNCC(gray, tplStart, THRESHOLD, matchX, matchY, matchScore)) {
            DebugLog(L"FAIL: tplStart not matched (bestScore=" + std::to_wstring(matchScore) + L")");
            return false;
        }
        DebugLog(L"OK: tplStart matched at (" + std::to_wstring(matchX) + L"," + std::to_wstring(matchY)
                 + L") score=" + std::to_wstring(matchScore));

        // キャプチャ領域計算 (C#と同一オフセット)
        int baseX = matchX + rect.left;
        int baseY = matchY + rect.top;

        constexpr int OFFSET_X_START = -3;
        constexpr int OFFSET_Y_START = -6;
        constexpr int OFFSET_X_END = 265;
        constexpr int OFFSET_Y_END = 490;

        int startX = baseX + OFFSET_X_START;
        int startY = baseY + OFFSET_Y_START;
        int captureWidth = (OFFSET_X_END - OFFSET_X_START) - 4;
        int captureHeight = OFFSET_Y_END - OFFSET_Y_START;

        DebugLog(L"Capture region: x=" + std::to_wstring(startX) + L" y=" + std::to_wstring(startY)
                 + L" w=" + std::to_wstring(captureWidth) + L" h=" + std::to_wstring(captureHeight));

        if (captureWidth <= 0 || captureHeight <= 0) {
            DebugLog(L"FAIL: invalid capture size");
            return false;
        }
        if (startX < 0) startX = 0;
        if (startY < 0) startY = 0;

        // 領域再キャプチャ
        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, captureWidth, captureHeight);
        HGDIOBJ hOld = SelectObject(hdcMem, hBmp);
        BitBlt(hdcMem, 0, 0, captureWidth, captureHeight, hdcScreen, startX, startY, SRCCOPY);
        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);

        // PNG で保存
        std::wstring saveDir = GetExeDirectory() + L"\\ログ\\露店開始画像";
        try { fs::create_directories(saveDir); } catch (...) {
            DebugLog(L"WARN: create_directories failed");
        }

        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t fileName[64];
        std::swprintf(fileName, 64, L"capture_%04d%02d%02d_%02d%02d%02d.png",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        std::wstring savePath = saveDir + L"\\" + fileName;

        DebugLog(L"Saving to: " + savePath);
        bool saved = SaveBitmapAsPng(hBmp, captureWidth, captureHeight, savePath);
        DeleteObject(hBmp);

        if (!saved) {
            DebugLog(L"FAIL: SaveBitmapAsPng failed");
            return false;
        }

        DebugLog(L"OK: Image saved successfully");

        // 古い画像を整理
        auto limit = LoadImageLimitConfig();
        CleanupOldImages(limit);

        DebugLog(L"=== CaptureRotenStartArea END (success) ===");
        return true;
    } catch (...) {
        DebugLog(L"EXCEPTION in CaptureRotenStartArea");
        return false;
    }
}

// ═══════════════════════════════════════════════════════════════════
// 画像保存上限設定の読み書き
// ═══════════════════════════════════════════════════════════════════
int RotenProcessor::LoadImageLimitConfig() {
    try {
        auto path = GetExeDirectory() + L"\\logger_config.ini";
        std::wifstream ifs(path);
        if (!ifs.is_open()) return 20;
        std::wstring line;
        while (std::getline(ifs, line)) {
            // trim
            while (!line.empty() && (line[0] == L' ' || line[0] == L'\t')) line.erase(line.begin());
            if (line.substr(0, 11) == L"image_limit") {
                auto eq = line.find(L'=');
                if (eq != std::wstring::npos) {
                    try { int v = std::stoi(line.substr(eq + 1)); return (v >= 1) ? v : 20; }
                    catch (...) {}
                }
            }
        }
    } catch (...) {}
    return 20;
}

void RotenProcessor::SaveImageLimitConfig(int limit) {
    try {
        auto path = GetExeDirectory() + L"\\logger_config.ini";
        std::vector<std::wstring> lines;
        {
            std::wifstream ifs(path);
            std::wstring l;
            while (std::getline(ifs, l)) lines.push_back(l);
        }
        bool found = false;
        for (auto& l : lines) {
            std::wstring trimmed = l;
            while (!trimmed.empty() && (trimmed[0] == L' ' || trimmed[0] == L'\t')) trimmed.erase(trimmed.begin());
            if (trimmed.substr(0, 11) == L"image_limit") {
                l = L"image_limit = " + std::to_wstring(limit);
                found = true;
                break;
            }
        }
        if (!found) lines.push_back(L"image_limit = " + std::to_wstring(limit));

        std::wofstream ofs(path);
        for (const auto& l : lines) ofs << l << L"\n";
    } catch (...) {}
}

std::vector<std::wstring> RotenProcessor::GetSortedImageFiles() {
    std::vector<std::wstring> files;
    try {
        auto dir = GetExeDirectory() + L"\\ログ\\露店開始画像";
        if (!fs::exists(dir)) return files;
        for (auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().wstring();
                // .png と .bmp の両方をサポート
                std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                if (ext == L".png" || ext == L".bmp")
                    files.push_back(entry.path().wstring());
            }
        }
        // 新しい順（更新日時の降順）
        std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) {
            try {
                return fs::last_write_time(a) > fs::last_write_time(b);
            } catch (...) { return a > b; }
        });
    } catch (...) {}
    return files;
}

void RotenProcessor::CleanupOldImages(int limit) {
    try {
        auto files = GetSortedImageFiles();
        if ((int)files.size() <= limit) return;
        for (int i = limit; i < (int)files.size(); i++) {
            try { fs::remove(files[i]); } catch (...) {}
        }
    } catch (...) {}
}

void RotenProcessor::RefreshViewerIfOpen() {
    if (!viewerHwnd_) return;
    imageFiles_ = GetSortedImageFiles();
    if (!imageFiles_.empty())
        DisplayImageAt(0);
    else
        UpdateViewerButtons();
}

// ═══════════════════════════════════════════════════════════════════
// 画像ビューアウィンドウ
// ═══════════════════════════════════════════════════════════════════
bool RotenProcessor::s_viewerClassRegistered = false;

// ビューア内コントロール ID
#define IDC_VIEWER_PREV   3001
#define IDC_VIEWER_NEXT   3002
#define IDC_VIEWER_SPIN   3003
#define IDC_VIEWER_SAVE   3004
#define IDC_VIEWER_PIC    3005
#define IDC_VIEWER_INFO   3006
#define IDC_VIEWER_LBL    3007

LRESULT CALLBACK RotenProcessor::ViewerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<RotenProcessor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_COMMAND:
        if (self) {
            switch (LOWORD(wParam)) {
            case IDC_VIEWER_PREV:
                if (self->currentImageIndex_ > 0)
                    self->DisplayImageAt(self->currentImageIndex_ - 1);
                return 0;
            case IDC_VIEWER_NEXT:
                if (self->currentImageIndex_ < (int)self->imageFiles_.size() - 1)
                    self->DisplayImageAt(self->currentImageIndex_ + 1);
                return 0;
            case IDC_VIEWER_SAVE:
            {
                BOOL success = FALSE;
                int val = (int)SendMessage(self->viewerLimitSpin_, UDM_GETPOS32, 0, (LPARAM)&success);
                if (success && val >= 1) {
                    SaveImageLimitConfig(val);
                    self->CleanupOldImages(val);
                    self->imageFiles_ = GetSortedImageFiles();
                    if (!self->imageFiles_.empty()) {
                        if (self->currentImageIndex_ >= (int)self->imageFiles_.size())
                            self->currentImageIndex_ = (int)self->imageFiles_.size() - 1;
                        self->DisplayImageAt(self->currentImageIndex_);
                    } else {
                        self->UpdateViewerButtons();
                    }
                }
                return 0;
            }
            }
        }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        // 背景描画
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH hBr = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
        FillRect(hdc, &rc, hBr);
        DeleteObject(hBr);

        // 画像描画（上部ラベル28px + 下部ボタンパネル90px を除いた領域）
        if (self && self->viewerBitmap_) {
            BITMAP bm;
            GetObject(self->viewerBitmap_, sizeof(bm), &bm);

            int areaTop = 28;
            int areaBot = rc.bottom - 90;
            int areaW = rc.right;
            int areaH = areaBot - areaTop;
            if (areaW > 0 && areaH > 0) {
                // Zoom to fit (aspect ratio preserved)
                double scaleX = (double)areaW / bm.bmWidth;
                double scaleY = (double)areaH / bm.bmHeight;
                double scale = (scaleX < scaleY) ? scaleX : scaleY;
                int dw = (int)(bm.bmWidth * scale);
                int dh = (int)(bm.bmHeight * scale);
                int dx = (areaW - dw) / 2;
                int dy = areaTop + (areaH - dh) / 2;

                HDC hdcMem = CreateCompatibleDC(hdc);
                HGDIOBJ hOld = SelectObject(hdcMem, self->viewerBitmap_);
                SetStretchBltMode(hdc, HALFTONE);
                StretchBlt(hdc, dx, dy, dw, dh, hdcMem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                SelectObject(hdcMem, hOld);
                DeleteDC(hdcMem);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        if (self) self->DestroyViewer();
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RotenProcessor::ToggleImageViewer(HWND hParent, HWND hViewerBtn) {
    if (viewerHwnd_) {
        DestroyViewer();
        return;
    }
    CreateViewerWindow(hParent, hViewerBtn);
}

void RotenProcessor::CreateViewerWindow(HWND hParent, HWND hViewerBtn) {
    viewerBtnHandle_ = hViewerBtn;

    // ウィンドウクラス登録
    if (!s_viewerClassRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ViewerWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
        wc.lpszClassName = L"RotenImageViewer";
        RegisterClassExW(&wc);
        s_viewerClassRegistered = true;
    }

    int viewerW = 300, viewerH = 600;

    // メインウィンドウの位置に基づいてビューアを左右に配置
    int newX = CW_USEDEFAULT, newY = CW_USEDEFAULT;
    if (hParent) {
        RECT mainRect;
        GetWindowRect(hParent, &mainRect);
        HMONITOR hMon = MonitorFromWindow(hParent, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(hMon, &mi);
        RECT work = mi.rcWork;

        int mainCenterX = mainRect.left + (mainRect.right - mainRect.left) / 2;
        int screenCenterX = work.left + (work.right - work.left) / 2;

        if (mainCenterX < screenCenterX) {
            // メインが左寄り → ビューアは右隣
            newX = mainRect.right;
            if (newX + viewerW > work.right) newX = work.right - viewerW;
        } else {
            // メインが右寄り → ビューアは左隣
            newX = mainRect.left - viewerW;
            if (newX < work.left) newX = work.left;
        }
        newY = mainRect.top;
        if (newY < work.top) newY = work.top;
        if (newY + viewerH > work.bottom) newY = (std::max)((int)work.top, (int)(work.bottom - viewerH));
    }

    viewerHwnd_ = CreateWindowExW(0, L"RotenImageViewer", L"露店開始画像ビューア",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        newX, newY, viewerW, viewerH,
        hParent, nullptr, GetModuleHandleW(nullptr), nullptr);

    SetWindowLongPtrW(viewerHwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // アイコン設定
    {
        HICON hIco = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON));
        if (hIco) {
            SendMessage(viewerHwnd_, WM_SETICON, ICON_BIG, (LPARAM)hIco);
            SendMessage(viewerHwnd_, WM_SETICON, ICON_SMALL, (LPARAM)hIco);
        }
    }

    HFONT hFont = (HFONT)SendMessage(hParent, WM_GETFONT, 0, 0);
    if (!hFont) hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // 情報ラベル (上部)
    viewerInfoLabel_ = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 4, viewerW - 16, 20, viewerHwnd_, (HMENU)IDC_VIEWER_INFO, nullptr, nullptr);
    SendMessage(viewerInfoLabel_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 下部コントロールパネル (ボタン + 設定)
    RECT crc;
    GetClientRect(viewerHwnd_, &crc);
    int clientW = crc.right;
    int bottomY = crc.bottom;

    // 前へ/次へボタン
    int btnW = 80, btnH = 28;
    viewerPrevBtn_ = CreateWindowExW(0, L"BUTTON", L"前へ",
        WS_CHILD | WS_VISIBLE,
        8, bottomY - 82, btnW, btnH, viewerHwnd_, (HMENU)IDC_VIEWER_PREV, nullptr, nullptr);
    viewerNextBtn_ = CreateWindowExW(0, L"BUTTON", L"次へ",
        WS_CHILD | WS_VISIBLE,
        clientW - btnW - 8, bottomY - 82, btnW, btnH, viewerHwnd_, (HMENU)IDC_VIEWER_NEXT, nullptr, nullptr);
    SendMessage(viewerPrevBtn_, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(viewerNextBtn_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 画像保存上限数ラベル
    HWND hLbl = CreateWindowExW(0, L"STATIC", L"画像保存上限数:",
        WS_CHILD | WS_VISIBLE,
        8, bottomY - 44, 100, 20, viewerHwnd_, (HMENU)IDC_VIEWER_LBL, nullptr, nullptr);
    SendMessage(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

    // UpDown + Edit
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_LEFT,
        112, bottomY - 46, 50, 22, viewerHwnd_, nullptr, nullptr, nullptr);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

    viewerLimitSpin_ = CreateWindowExW(0, UPDOWN_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS,
        0, 0, 0, 0, viewerHwnd_, (HMENU)IDC_VIEWER_SPIN, nullptr, nullptr);
    SendMessage(viewerLimitSpin_, UDM_SETBUDDY, (WPARAM)hEdit, 0);
    SendMessage(viewerLimitSpin_, UDM_SETRANGE32, 1, 1000);
    SendMessage(viewerLimitSpin_, UDM_SETPOS32, 0, LoadImageLimitConfig());

    // 設定変更ボタン
    viewerSaveBtn_ = CreateWindowExW(0, L"BUTTON", L"設定を変更する",
        WS_CHILD | WS_VISIBLE,
        170, bottomY - 46, 100, 22, viewerHwnd_, (HMENU)IDC_VIEWER_SAVE, nullptr, nullptr);
    SendMessage(viewerSaveBtn_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // ボタンテキスト変更
    if (viewerBtnHandle_)
        SetWindowTextW(viewerBtnHandle_, L"露店開始画像を閉じる");

    // 画像読み込み
    imageFiles_ = GetSortedImageFiles();
    currentImageIndex_ = 0;
    if (!imageFiles_.empty())
        DisplayImageAt(0);
    else
        UpdateViewerButtons();

    ShowWindow(viewerHwnd_, SW_SHOW);
    UpdateWindow(viewerHwnd_);
}

void RotenProcessor::DestroyViewer() {
    if (viewerBitmap_) {
        DeleteObject(viewerBitmap_);
        viewerBitmap_ = nullptr;
    }
    if (viewerHwnd_) {
        DestroyWindow(viewerHwnd_);
        viewerHwnd_ = nullptr;
    }
    viewerPicture_ = nullptr;
    viewerInfoLabel_ = nullptr;
    viewerPrevBtn_ = nullptr;
    viewerNextBtn_ = nullptr;
    viewerLimitSpin_ = nullptr;
    viewerSaveBtn_ = nullptr;

    if (viewerBtnHandle_) {
        SetWindowTextW(viewerBtnHandle_, L"露店開始画像を表示");
        viewerBtnHandle_ = nullptr;
    }
}

void RotenProcessor::DisplayImageAt(int idx) {
    if (!viewerHwnd_) return;
    if (imageFiles_.empty()) {
        if (viewerInfoLabel_) SetWindowTextW(viewerInfoLabel_, L"画像がありません");
        if (viewerBitmap_) { DeleteObject(viewerBitmap_); viewerBitmap_ = nullptr; }
        UpdateViewerButtons();
        InvalidateRect(viewerHwnd_, nullptr, TRUE);
        return;
    }
    if (idx < 0) idx = 0;
    if (idx >= (int)imageFiles_.size()) idx = (int)imageFiles_.size() - 1;
    currentImageIndex_ = idx;

    // 古いビットマップを解放
    if (viewerBitmap_) { DeleteObject(viewerBitmap_); viewerBitmap_ = nullptr; }

    // GDI+ で画像読み込み（ファイルロックしない方式）
    try {
        Gdiplus::Bitmap gdipBmp(imageFiles_[idx].c_str());
        if (gdipBmp.GetLastStatus() == Gdiplus::Ok) {
            gdipBmp.GetHBITMAP(Gdiplus::Color(0, 0, 0), &viewerBitmap_);
        }
    } catch (...) {}

    // 情報ラベル更新
    if (viewerInfoLabel_) {
        auto fname = fs::path(imageFiles_[idx]).filename().wstring();
        wchar_t buf[256];
        std::swprintf(buf, 256, L"%d/%d - %s", idx + 1, (int)imageFiles_.size(), fname.c_str());
        SetWindowTextW(viewerInfoLabel_, buf);
    }

    UpdateViewerButtons();
    InvalidateRect(viewerHwnd_, nullptr, TRUE);
}

void RotenProcessor::UpdateViewerButtons() {
    if (viewerPrevBtn_)
        EnableWindow(viewerPrevBtn_, currentImageIndex_ > 0);
    if (viewerNextBtn_)
        EnableWindow(viewerNextBtn_, !imageFiles_.empty() && currentImageIndex_ < (int)imageFiles_.size() - 1);
}

void RotenProcessor::AppendLineToLog(int year, int month, int day, int hour, int minute,
                                      const std::wstring& line) {
    try {
        auto sjis = WideToSjis(line + L"\r\n");
        LogCallback logCallback;
        RotenLogEntry entry;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);

            // Date change check
            if (!dateValid_ || currentDate_[0] != year || currentDate_[1] != month || currentDate_[2] != day) {
                currentDate_[0] = year;
                currentDate_[1] = month;
                currentDate_[2] = day;
                dateValid_ = true;
                currentFileIndex_ = 0;
                currentLineCount_ = 0;
                currentLogFilePath_.clear();
            }

            if (currentLogFilePath_.empty()) {
                currentLogFilePath_ = GetLogFilePath(year, month, day, currentFileIndex_);
            }

            if (currentLineCount_ >= maxLinesPerFile_) {
                currentFileIndex_++;
                currentLogFilePath_ = GetLogFilePath(year, month, day, currentFileIndex_);
                currentLineCount_ = 0;
            }

            std::ofstream ofs(currentLogFilePath_, std::ios::app | std::ios::binary);
            if (ofs.is_open()) {
                ofs.write(sjis.data(), sjis.size());
                currentLineCount_++;
            }

            logCallback = logCallback_;
        }

        // UI callback
        if (logCallback) {
            wchar_t timeBuf[8];
            std::swprintf(timeBuf, 8, L"%02d:%02d", hour, minute);
            entry.timeText = timeBuf;

            // Extract message part (skip "MM/DD HH:MM " prefix)
            auto idx1 = line.find(L' ');
            auto idx2 = (idx1 != std::wstring::npos) ? line.find(L' ', idx1 + 1) : std::wstring::npos;
            if (idx2 != std::wstring::npos && idx2 + 1 < line.size())
                entry.message = line.substr(idx2 + 1);
            else
                entry.message = line;

            logCallback(entry);
        }
    } catch (...) {}
}

std::wstring RotenProcessor::GetLogFilePath(int year, int month, int day, int index) {
    // ログ/露店売買/yyyy年MM月分/yyyy_MM_dd_roten_0.txt
    wchar_t monthFolder[32];
    std::swprintf(monthFolder, 32, L"%04d年%02d月分", year, month);
    std::wstring dir = logDir_ + L"\\" + monthFolder;
    try { fs::create_directories(dir); } catch (...) {}

    wchar_t name[64];
    std::swprintf(name, 64, L"%04d_%02d_%02d_roten_%d.txt", year, month, day, index);
    return dir + L"\\" + name;
}

std::wstring RotenProcessor::FormatLogLine(int month, int day, int hour, int minute, const std::wstring& msg) {
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%02d/%02d %02d:%02d ", month, day, hour, minute);
    return std::wstring(buf) + msg;
}

std::wstring RotenProcessor::FormatJapaneseAmount(uint32_t amount) {
    if (amount == 0) return L"0";
    std::wstring parts;
    uint32_t oku = amount / 100000000;
    if (oku > 0) parts += std::to_wstring(oku) + L"億";
    amount %= 100000000;
    uint32_t man = amount / 10000;
    if (man > 0) parts += std::to_wstring(man) + L"万";
    amount %= 10000;
    if (amount > 0 || parts.empty()) parts += std::to_wstring(amount);
    return parts;
}
