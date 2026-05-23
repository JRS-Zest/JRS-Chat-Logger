#include "network_helper.h"
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

std::vector<NetworkInterfaceInfo> NetworkHelper::GetNetworkInterfaces() {
    std::vector<NetworkInterfaceInfo> result;

    ULONG bufLen = 15000;
    std::vector<BYTE> buffer(bufLen);
    auto pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG ret = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &bufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufLen);
        pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        ret = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &bufLen);
    }
    if (ret != NO_ERROR) return result;

    for (auto p = pAddresses; p; p = p->Next) {
        if (p->OperStatus != IfOperStatusUp) continue;

        NetworkInterfaceInfo info;
        info.name = p->FriendlyName ? p->FriendlyName : L"";
        info.description = p->Description ? p->Description : L"";
        info.guid = p->AdapterName ? p->AdapterName : "";
        info.ipAddress = L"IPなし";

        for (auto ua = p->FirstUnicastAddress; ua; ua = ua->Next) {
            if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                auto sa = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
                wchar_t ipBuf[46] = {};
                InetNtopW(AF_INET, &sa->sin_addr, ipBuf, 46);
                info.ipAddress = ipBuf;
                break;
            }
        }

        result.push_back(std::move(info));
    }

    return result;
}

std::wstring NetworkHelper::GetDefaultGatewayInterfaceName() {
    ULONG bufLen = 15000;
    std::vector<BYTE> buffer(bufLen);
    auto pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG ret = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &bufLen);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufLen);
        pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        ret = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &bufLen);
    }
    if (ret != NO_ERROR) return L"";

    // IPv4 ゲートウェイを優先
    for (auto p = pAddresses; p; p = p->Next) {
        if (p->OperStatus != IfOperStatusUp) continue;
        for (auto gw = p->FirstGatewayAddress; gw; gw = gw->Next) {
            if (gw->Address.lpSockaddr->sa_family == AF_INET) {
                auto sa = reinterpret_cast<sockaddr_in*>(gw->Address.lpSockaddr);
                if (sa->sin_addr.S_un.S_addr != 0) {
                    return p->FriendlyName ? p->FriendlyName : L"";
                }
            }
        }
    }

    // IPv6 フォールバック
    for (auto p = pAddresses; p; p = p->Next) {
        if (p->OperStatus != IfOperStatusUp) continue;
        for (auto gw = p->FirstGatewayAddress; gw; gw = gw->Next) {
            if (gw->Address.lpSockaddr->sa_family == AF_INET6) {
                return p->FriendlyName ? p->FriendlyName : L"";
            }
        }
    }

    return L"";
}
