#pragma once
#include <functional>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <set>
#include <cstdint>
#include <windows.h>

// C# PacketCaptureService.cs の移植
// Npcap (wpcap.dll) 動的ロード + Ethernet/IP/TCP ヘッダーパース

struct TcpPacketInfo {
    std::vector<uint8_t> payload;  // TCP ペイロード
    uint32_t srcIp = 0;
    uint32_t dstIp = 0;
    uint16_t srcPort = 0;
    uint16_t dstPort = 0;
    bool isIncoming = false;       // サーバー → クライアント
};

class PacketCaptureService {
public:
    using Callback = std::function<void(const TcpPacketInfo&)>;

    PacketCaptureService();
    ~PacketCaptureService();

    // キャプチャ開始 (deviceGuid: AdapterName (GUID) で pcap デバイスを照合)
    // targetServerIP: フィルタ対象 IP (e.g., "220.153.168.131")
    void Start(Callback cb, const std::string& deviceGuid,
               const std::string& targetServerIP, HWND notifyHwnd = NULL);
    void Stop();
    bool IsRunning() const;

    // ポートフィルタ (PID監視用)
    void SetPortFilter(const std::set<uint16_t>& ports);
    void ClearPortFilter();

    // デバイス列挙 (Npcap format: name, description ペア)
    struct DeviceInfo {
        std::string name;        // pcap デバイス名
        std::string description; // 説明文
    };
    static std::vector<DeviceInfo> EnumerateDevices();

private:
    void CaptureLoop(std::string deviceName, std::string targetIP);
    bool ParseEthernetIpTcp(const uint8_t* data, int len, TcpPacketInfo& out);

    std::thread thread_;
    std::atomic<bool> running_{false};
    Callback callback_;
    HWND notifyHwnd_ = NULL;
    std::string targetServerIP_;

    std::mutex portMtx_;
    std::set<uint16_t> portFilter_;
    bool usePortFilter_ = false;
};
