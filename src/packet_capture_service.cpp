#include "packet_capture_service.h"
#include <cstring>
#include <ws2tcpip.h>

// Npcap dynamic loading types (same as original packet_capture.cpp)
typedef unsigned int bpf_u_int32;
struct pcap_pkthdr { struct timeval ts; bpf_u_int32 caplen; bpf_u_int32 len; };
struct pcap_if_s {
    struct pcap_if_s* next;
    char* name;
    char* description;
    void* addresses;
    bpf_u_int32 flags;
};
using pcap_if = pcap_if_s;

using pcap_findalldevs_t = int(*)(pcap_if**, char*);
using pcap_freealldevs_t = void(*)(pcap_if*);
using pcap_open_live_t   = void*(*)(const char*, int, int, int, char*);
using pcap_next_ex_t     = int(*)(void*, pcap_pkthdr**, const unsigned char**);
using pcap_close_t       = void(*)(void*);
using pcap_setfilter_t   = int(*)(void*, void*);
using pcap_compile_t     = int(*)(void*, void*, const char*, int, bpf_u_int32);
using pcap_freecode_t    = void(*)(void*);

// BPF program structure
struct bpf_program_s {
    unsigned int bf_len;
    void* bf_insns;
};

PacketCaptureService::PacketCaptureService() {}

PacketCaptureService::~PacketCaptureService() {
    Stop();
}

bool PacketCaptureService::ParseEthernetIpTcp(const uint8_t* data, int len, TcpPacketInfo& out) {
    // Ethernet header: 14 bytes minimum
    if (len < 14) return false;

    // EtherType at offset 12-13
    uint16_t etherType = (static_cast<uint16_t>(data[12]) << 8) | data[13];

    int ipOffset = 14;

    // Handle VLAN tagging (802.1Q)
    if (etherType == 0x8100) {
        if (len < 18) return false;
        etherType = (static_cast<uint16_t>(data[16]) << 8) | data[17];
        ipOffset = 18;
    }

    // Only IPv4 (0x0800)
    if (etherType != 0x0800) return false;

    // IP header
    if (len < ipOffset + 20) return false;
    const uint8_t* ip = data + ipOffset;

    uint8_t version = (ip[0] >> 4) & 0x0F;
    if (version != 4) return false;

    uint8_t ihl = (ip[0] & 0x0F) * 4;
    if (ihl < 20) return false;

    uint8_t protocol = ip[9];
    if (protocol != 6) return false; // TCP only

    out.srcIp = (static_cast<uint32_t>(ip[12]) << 24) |
                (static_cast<uint32_t>(ip[13]) << 16) |
                (static_cast<uint32_t>(ip[14]) << 8)  |
                static_cast<uint32_t>(ip[15]);
    out.dstIp = (static_cast<uint32_t>(ip[16]) << 24) |
                (static_cast<uint32_t>(ip[17]) << 16) |
                (static_cast<uint32_t>(ip[18]) << 8)  |
                static_cast<uint32_t>(ip[19]);

    uint16_t totalLen = (static_cast<uint16_t>(ip[2]) << 8) | ip[3];

    // TCP header
    int tcpOffset = ipOffset + ihl;
    if (len < tcpOffset + 20) return false;
    const uint8_t* tcp = data + tcpOffset;

    out.srcPort = (static_cast<uint16_t>(tcp[0]) << 8) | tcp[1];
    out.dstPort = (static_cast<uint16_t>(tcp[2]) << 8) | tcp[3];

    uint8_t dataOffset = ((tcp[12] >> 4) & 0x0F) * 4;
    if (dataOffset < 20) return false;

    // TCP payload
    int payloadOffset = tcpOffset + dataOffset;
    int payloadLen = totalLen - ihl - dataOffset;
    if (payloadLen <= 0) return false;

    // Clamp to actual captured length
    int available = len - payloadOffset;
    if (available <= 0) return false;
    if (payloadLen > available) payloadLen = available;

    out.payload.assign(data + payloadOffset, data + payloadOffset + payloadLen);
    return true;
}

void PacketCaptureService::Start(Callback cb, const std::string& deviceGuid,
                                  const std::string& targetServerIP, HWND notifyHwnd) {
    if (running_) return;

    callback_ = std::move(cb);
    notifyHwnd_ = notifyHwnd;
    targetServerIP_ = targetServerIP;
    running_ = true;

    // Find pcap device name by GUID (pcap name contains GUID e.g. \Device\NPF_{GUID})
    std::string pcapDeviceName;
    auto devices = EnumerateDevices();
    for (auto& d : devices) {
        if (!deviceGuid.empty() && d.name.find(deviceGuid) != std::string::npos) {
            pcapDeviceName = d.name;
            break;
        }
    }
    if (pcapDeviceName.empty() && !devices.empty()) {
        pcapDeviceName = devices[0].name;
    }

    thread_ = std::thread([this, pcapName = std::move(pcapDeviceName), svrIP = targetServerIP]() {
        CaptureLoop(pcapName, svrIP);
    });
}

void PacketCaptureService::CaptureLoop(std::string deviceName, std::string targetIP) {
    // Npcap は System32\Npcap にインストールされるため検索パスに追加
    SetDllDirectoryA("C:\\Windows\\System32\\Npcap");
    HMODULE hWpcap = LoadLibraryA("wpcap.dll");
    SetDllDirectoryA(nullptr); // 検索パスをデフォルトに戻す
    if (!hWpcap) {
        OutputDebugStringW(L"wpcap.dll のロードに失敗。Npcap がインストールされていません。\n");
        running_ = false;
        if (notifyHwnd_) PostMessageW(notifyHwnd_, WM_APP + 6, 0, 0);
        return;
    }

    auto p_findalldevs = reinterpret_cast<pcap_findalldevs_t>(GetProcAddress(hWpcap, "pcap_findalldevs"));
    auto p_freealldevs = reinterpret_cast<pcap_freealldevs_t>(GetProcAddress(hWpcap, "pcap_freealldevs"));
    auto p_open_live   = reinterpret_cast<pcap_open_live_t>(GetProcAddress(hWpcap, "pcap_open_live"));
    auto p_next_ex     = reinterpret_cast<pcap_next_ex_t>(GetProcAddress(hWpcap, "pcap_next_ex"));
    auto p_close       = reinterpret_cast<pcap_close_t>(GetProcAddress(hWpcap, "pcap_close"));
    auto p_compile     = reinterpret_cast<pcap_compile_t>(GetProcAddress(hWpcap, "pcap_compile"));
    auto p_setfilter   = reinterpret_cast<pcap_setfilter_t>(GetProcAddress(hWpcap, "pcap_setfilter"));
    auto p_freecode    = reinterpret_cast<pcap_freecode_t>(GetProcAddress(hWpcap, "pcap_freecode"));

    if (!(p_findalldevs && p_freealldevs && p_open_live && p_next_ex && p_close)) {
        FreeLibrary(hWpcap);
        running_ = false;
        if (notifyHwnd_) PostMessageW(notifyHwnd_, WM_APP + 6, 0, 0);
        return;
    }

    char errbuf[256] = {};
    void* handle = p_open_live(deviceName.c_str(), 65536, 0, 100, errbuf);
    if (!handle) {
        OutputDebugStringW((L"pcap_open_live 失敗: " + std::wstring(errbuf, errbuf + strlen(errbuf)) + L"\n").c_str());
        FreeLibrary(hWpcap);
        running_ = false;
        if (notifyHwnd_) PostMessageW(notifyHwnd_, WM_APP + 6, 0, 0);
        return;
    }

    // BPF フィルタ設定: "tcp and host <targetIP>"
    if (p_compile && p_setfilter && p_freecode && !targetIP.empty()) {
        std::string filter = "tcp and host " + targetIP;
        bpf_program_s fp = {};
        if (p_compile(handle, &fp, filter.c_str(), 1, 0xFFFFFFFF) == 0) {
            p_setfilter(handle, &fp);
            p_freecode(&fp);
        }
    }

    // Notify UI that capture is running
    if (notifyHwnd_) PostMessageW(notifyHwnd_, WM_APP + 6, 1, 0);

    // Parse targetIP to uint32 for isIncoming detection
    uint32_t serverIpNum = 0;
    if (!targetIP.empty()) {
        auto parts = targetIP;
        uint8_t octets[4] = {};
        size_t pos = 0;
        for (int i = 0; i < 4 && pos < parts.size(); i++) {
            auto dot = parts.find('.', pos);
            std::string seg = (dot == std::string::npos) ? parts.substr(pos) : parts.substr(pos, dot - pos);
            octets[i] = static_cast<uint8_t>(std::stoi(seg));
            pos = (dot == std::string::npos) ? parts.size() : dot + 1;
        }
        serverIpNum = (static_cast<uint32_t>(octets[0]) << 24) |
                      (static_cast<uint32_t>(octets[1]) << 16) |
                      (static_cast<uint32_t>(octets[2]) << 8)  |
                      static_cast<uint32_t>(octets[3]);
    }

    while (running_) {
        pcap_pkthdr* hdr = nullptr;
        const unsigned char* data = nullptr;
        int res = p_next_ex(handle, &hdr, &data);
        if (res == 1 && hdr && data) {
            TcpPacketInfo info;
            if (ParseEthernetIpTcp(data, static_cast<int>(hdr->caplen), info)) {
                // Determine direction
                info.isIncoming = (info.srcIp == serverIpNum);

                // Port filter check
                bool passFilter = true;
                {
                    std::lock_guard<std::mutex> lk(portMtx_);
                    if (usePortFilter_ && !portFilter_.empty()) {
                        passFilter = portFilter_.count(info.srcPort) > 0 ||
                                     portFilter_.count(info.dstPort) > 0;
                    }
                }

                if (passFilter && callback_) {
                    callback_(info);
                }
            }
        } else if (res == 0) {
            continue; // timeout
        } else {
            break; // error
        }
    }

    p_close(handle);
    FreeLibrary(hWpcap);
    running_ = false;
    if (notifyHwnd_) PostMessageW(notifyHwnd_, WM_APP + 6, 0, 0);
}

void PacketCaptureService::Stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

bool PacketCaptureService::IsRunning() const {
    return running_;
}

void PacketCaptureService::SetPortFilter(const std::set<uint16_t>& ports) {
    std::lock_guard<std::mutex> lk(portMtx_);
    portFilter_ = ports;
    usePortFilter_ = true;
}

void PacketCaptureService::ClearPortFilter() {
    std::lock_guard<std::mutex> lk(portMtx_);
    portFilter_.clear();
    usePortFilter_ = false;
}

std::vector<PacketCaptureService::DeviceInfo> PacketCaptureService::EnumerateDevices() {
    std::vector<DeviceInfo> result;
    SetDllDirectoryA("C:\\Windows\\System32\\Npcap");
    HMODULE hWpcap = LoadLibraryA("wpcap.dll");
    SetDllDirectoryA(nullptr);
    if (!hWpcap) return result;

    auto p_findalldevs = reinterpret_cast<pcap_findalldevs_t>(GetProcAddress(hWpcap, "pcap_findalldevs"));
    auto p_freealldevs = reinterpret_cast<pcap_freealldevs_t>(GetProcAddress(hWpcap, "pcap_freealldevs"));
    if (!(p_findalldevs && p_freealldevs)) {
        FreeLibrary(hWpcap);
        return result;
    }

    char errbuf[256] = {};
    pcap_if* alldevs = nullptr;
    if (p_findalldevs(&alldevs, errbuf) != 0 || !alldevs) {
        FreeLibrary(hWpcap);
        return result;
    }

    for (auto it = alldevs; it; it = it->next) {
        DeviceInfo di;
        di.name = it->name ? it->name : "";
        di.description = it->description ? it->description : "";
        result.push_back(std::move(di));
    }

    p_freealldevs(alldevs);
    FreeLibrary(hWpcap);
    return result;
}
