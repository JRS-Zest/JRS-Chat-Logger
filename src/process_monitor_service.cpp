#include "process_monitor_service.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tcpmib.h>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "iphlpapi.lib")

// C# ProcessMonitorService.cs の完全移植
// netstat の代わりに GetExtendedTcpTable API を使用（より効率的）

ProcessMonitorService::ProcessMonitorService(const std::wstring& targetServerIp)
    : targetServerIp_(targetServerIp)
{
}

ProcessMonitorService::~ProcessMonitorService() {
    StopMonitoring();
}

void ProcessMonitorService::StartMonitoring(int pid) {
    {
        std::lock_guard<std::mutex> lk(lock_);
        if (isMonitoring_) {
            // 一旦停止（ロック内でpollingは触らない）
        }
    }
    StopMonitoring();

    {
        std::lock_guard<std::mutex> lk(lock_);
        monitoredPid_ = pid;
        monitoredPorts_.clear();
        monitoredRemoteIp_.clear();
        isMonitoring_ = true;
    }

    // 初回ポート検出
    UpdatePorts();

    // ポーリング開始
    pollingRunning_ = true;
    pollingThread_ = std::thread(&ProcessMonitorService::PollingLoop, this);

    if (stateChanged_) {
        try { stateChanged_(true, pid); } catch (...) {}
    }
}

void ProcessMonitorService::StopMonitoring() {
    pollingRunning_ = false;
    if (pollingThread_.joinable()) {
        pollingThread_.join();
    }

    {
        std::lock_guard<std::mutex> lk(lock_);
        monitoredPid_ = 0;
        monitoredPorts_.clear();
        monitoredRemoteIp_.clear();
        isMonitoring_ = false;
    }

    // 空のポート集合を通知（フィルタ解除）
    if (portsUpdated_) {
        try { portsUpdated_(std::set<int>{}, L""); } catch (...) {}
    }
    if (stateChanged_) {
        try { stateChanged_(false, 0); } catch (...) {}
    }
}

bool ProcessMonitorService::IsPortMonitored(int port) const {
    if (!isMonitoring_) return true; // 監視していない場合は全て許可
    std::lock_guard<std::mutex> lk(lock_);
    return monitoredPorts_.count(port) > 0;
}

std::set<int> ProcessMonitorService::GetMonitoredPorts() const {
    std::lock_guard<std::mutex> lk(lock_);
    return monitoredPorts_;
}

void ProcessMonitorService::PollingLoop() {
    while (pollingRunning_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!pollingRunning_) break;
        UpdatePorts();
    }
}

// IPv4 DWORD → L"x.x.x.x" 変換
static std::wstring DwordToIpString(DWORD addr) {
    wchar_t buf[32];
    swprintf_s(buf, L"%u.%u.%u.%u",
               addr & 0xFF, (addr >> 8) & 0xFF,
               (addr >> 16) & 0xFF, (addr >> 24) & 0xFF);
    return buf;
}

void ProcessMonitorService::UpdatePorts() {
    if (!isMonitoring_ || monitoredPid_ == 0) return;

    try {
        auto connections = GetTcpConnectionsForPid(monitoredPid_, targetServerIp_);

        std::set<int> newPorts;
        std::wstring newRemoteIp;
        for (auto& conn : connections) {
            newPorts.insert(conn.localPort);
            if (conn.remoteAddress == targetServerIp_) {
                newRemoteIp = conn.remoteAddress;
            }
        }

        bool changed = false;
        {
            std::lock_guard<std::mutex> lk(lock_);
            if (monitoredPorts_ != newPorts || monitoredRemoteIp_ != newRemoteIp) {
                monitoredPorts_ = newPorts;
                monitoredRemoteIp_ = newRemoteIp;
                changed = true;
            }
        }

        if (changed && portsUpdated_) {
            try { portsUpdated_(newPorts, newRemoteIp); } catch (...) {}
        }
    } catch (...) {}
}

std::vector<ProcessMonitorService::TcpConnectionInfo>
ProcessMonitorService::GetTcpConnectionsForPid(int pid, const std::wstring& targetIp) {
    std::vector<TcpConnectionInfo> result;

    // GetExtendedTcpTable を使用（netstatよりも効率的）
    DWORD size = 0;
    GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (size == 0) return result;

    std::vector<uint8_t> buffer(size);
    DWORD ret = GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (ret != NO_ERROR) return result;

    auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    for (DWORD i = 0; i < table->dwNumEntries; i++) {
        auto& row = table->table[i];
        if (static_cast<int>(row.dwOwningPid) != pid) continue;
        if (row.dwState != MIB_TCP_STATE_ESTAB) continue;

        auto remoteIp = DwordToIpString(row.dwRemoteAddr);
        if (remoteIp == targetIp) {
            TcpConnectionInfo info;
            info.localPort = ntohs(static_cast<uint16_t>(row.dwLocalPort));
            info.remoteAddress = remoteIp;
            result.push_back(info);
        }
    }

    return result;
}
