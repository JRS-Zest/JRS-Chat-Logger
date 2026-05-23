#pragma once
#include <string>
#include <set>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

// C# ProcessMonitorService.cs の完全移植
// 指定PIDが使用するポートを監視し、パケットフィルタリングに使用

class ProcessMonitorService {
public:
    using PortsUpdatedFunc = std::function<void(const std::set<int>& ports, const std::wstring& remoteIp)>;
    using StateChangedFunc = std::function<void(bool monitoring, int pid)>;

    ProcessMonitorService(const std::wstring& targetServerIp);
    ~ProcessMonitorService();

    void SetPortsUpdatedCallback(PortsUpdatedFunc cb) { portsUpdated_ = std::move(cb); }
    void SetStateChangedCallback(StateChangedFunc cb) { stateChanged_ = std::move(cb); }

    void StartMonitoring(int pid);
    void StopMonitoring();
    bool IsPortMonitored(int port) const;

    int GetCurrentPid() const { return monitoredPid_; }
    bool IsMonitoring() const { return isMonitoring_; }
    std::set<int> GetMonitoredPorts() const;
    std::wstring GetMonitoredRemoteIp() const {
        std::lock_guard<std::mutex> lk(lock_);
        return monitoredRemoteIp_;
    }

private:
    struct TcpConnectionInfo {
        int localPort;
        std::wstring remoteAddress;
    };

    void PollingLoop();
    void UpdatePorts();
    static std::vector<TcpConnectionInfo> GetTcpConnectionsForPid(int pid, const std::wstring& targetIp);

    std::wstring targetServerIp_;
    int monitoredPid_ = 0;
    std::set<int> monitoredPorts_;
    std::wstring monitoredRemoteIp_;
    mutable std::mutex lock_;
    std::atomic<bool> isMonitoring_{false};
    std::atomic<bool> pollingRunning_{false};
    std::thread pollingThread_;

    PortsUpdatedFunc portsUpdated_;
    StateChangedFunc stateChanged_;
};
