#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <windows.h>

class ItemDatLoader;

// 露店売買のログエントリ
struct RotenLogEntry {
    std::wstring timeText;    // HH:MM
    std::wstring message;     // ログメッセージ本文
};

// グレースケール画像データ（テンプレートマッチング用）
struct GrayImage {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    bool empty() const { return data.empty() || width <= 0 || height <= 0; }
};

// C# RotenProcessor の移植
// 露店売買パケット（販売/購入/露店開始）を解析し、テキストログ保存 + UIコールバック
class RotenProcessor {
public:
    using LogCallback = std::function<void(const RotenLogEntry& entry)>;
    using CaptureCallback = std::function<void()>;  // 露店開始画像キャプチャ要求

    explicit RotenProcessor(ItemDatLoader& itemDatLoader, int maxLinesPerFile = 1000);

    void SetLogCallback(LogCallback cb) { logCallback_ = std::move(cb); }
    void SetCaptureCallback(CaptureCallback cb) { captureCallback_ = std::move(cb); }

    void ProcessPacket(const uint8_t* data, int len, bool isIncoming, int srcPort, int dstPort);
    void ResetState();

    // 手動スナップ（UIスレッドから呼ぶ）
    bool CaptureRotenStartArea();

    // 画像ビューア
    // hParent: メインウィンドウ、開閉トグル。ビューアが開いていればhViewerBtnのテキストを更新
    void ToggleImageViewer(HWND hParent, HWND hViewerBtn);
    bool IsViewerOpen() const { return viewerHwnd_ != nullptr; }
    HWND GetViewerHwnd() const { return viewerHwnd_; }

    // ビューアが開かれている場合、最新画像リストに更新
    void RefreshViewerIfOpen();

    // 画像保存上限の読み書き
    static int  LoadImageLimitConfig();
    static void SaveImageLimitConfig(int limit);
    // 古い画像の削除
    void CleanupOldImages(int limit);
    // 画像ファイル一覧（新しい順）
    static std::vector<std::wstring> GetSortedImageFiles();

private:
    static constexpr uint8_t COMMAND_ID[2]     = {0x28, 0x11};
    static constexpr uint8_t PRE_COMMAND_ID[2] = {0x91, 0x10};
    static constexpr uint16_t SUB_COMMAND_SALE           = 0x1209;
    static constexpr uint16_t SUB_COMMAND_PURCHASE       = 0x1208;
    static constexpr uint16_t SUB_COMMAND_START_ROTEN    = 0x1207;

    static constexpr double DUPLICATE_WINDOW_SEC  = 0.5;
    static constexpr double CLEANUP_THRESHOLD_SEC = 5.0;
    static constexpr double PRECEDING_TIMEOUT_SEC = 0.3;

    ItemDatLoader& itemDatLoader_;
    LogCallback logCallback_;
    CaptureCallback captureCallback_;

    std::wstring logDir_;
    int maxLinesPerFile_;
    mutable std::mutex stateMutex_;

    // ログファイル管理
    int currentDate_[3] = {0, 0, 0};
    bool dateValid_ = false;
    int currentFileIndex_ = 0;
    int currentLineCount_ = 0;
    std::wstring currentLogFilePath_;

    // 重複検出
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastPackets_;

    // 先行パケット検出
    std::chrono::steady_clock::time_point lastPrecedingTime_;
    bool precedingReceived_ = false;

    // キャプチャクールダウン
    bool captureCooldown_ = false;

    // パケット解析
    bool TryParseSalePacket(const uint8_t* data, int len,
                            std::wstring& purchaserName, uint32_t& salesAmount,
                            uint16_t& slotNumber, uint8_t& currencyType);
    bool TryParsePurchasePacket(const uint8_t* data, int len,
                                std::wstring& ownerName, uint32_t& price,
                                uint16_t& itemSerial, uint8_t& currencyType,
                                std::wstring& optionString);

    // ログ書き込み
    void AppendLineToLog(int year, int month, int day, int hour, int minute,
                         const std::wstring& line);
    std::wstring GetLogFilePath(int year, int month, int day, int index);
    std::wstring FormatLogLine(int month, int day, int hour, int minute, const std::wstring& msg);
    static std::wstring FormatJapaneseAmount(uint32_t amount);

    // 重複チェック
    bool IsDuplicate(const uint8_t* data, int len);
    void CleanupOldEntries();

    // テンプレートマッチング (NCC)
    static GrayImage LoadTemplateFromResource(int resourceId);
    static GrayImage CaptureWindowGray(HWND hwnd, RECT& outRect);
    static bool MatchTemplateNCC(const GrayImage& src, const GrayImage& tpl,
                                 double threshold, int& outX, int& outY, double& outScore,
                                 int searchStartY = 0);
    static GrayImage BmpBytesToGray(const uint8_t* data, int dataSize);

    // PNG保存（GDI+使用）
    static bool SaveBitmapAsPng(HBITMAP hBmp, int w, int h, const std::wstring& path);

    // 画像ビューア関連
    HWND viewerHwnd_ = nullptr;         // ビューアウィンドウ
    HWND viewerPicture_ = nullptr;      // 画像表示用Static
    HWND viewerInfoLabel_ = nullptr;    // "1/5 - filename" 表示
    HWND viewerPrevBtn_ = nullptr;
    HWND viewerNextBtn_ = nullptr;
    HWND viewerLimitSpin_ = nullptr;    // 上限数入力
    HWND viewerSaveBtn_ = nullptr;      // 設定変更ボタン
    HWND viewerBtnHandle_ = nullptr;    // トグルボタン(メイン側)のハンドル

    std::vector<std::wstring> imageFiles_;
    int currentImageIndex_ = 0;
    HBITMAP viewerBitmap_ = nullptr;    // 現在表示中のビットマップ

    void CreateViewerWindow(HWND hParent, HWND hViewerBtn);
    void DestroyViewer();
    void DisplayImageAt(int idx);
    void UpdateViewerButtons();

    static LRESULT CALLBACK ViewerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static bool s_viewerClassRegistered;
};
