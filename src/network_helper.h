#pragma once
#include <string>
#include <vector>

// C# NetworkHelper.cs の移植
// Windows API (GetAdaptersAddresses) でネットワークインターフェースを列挙

struct NetworkInterfaceInfo {
    std::wstring name;         // フレンドリー名 (FriendlyName)
    std::wstring description;  // アダプタ説明 (ドライバ名)
    std::string  guid;         // AdapterName (GUID) — pcap デバイス照合用
    std::wstring ipAddress;    // IPv4
};

class NetworkHelper {
public:
    // 稼働中インターフェース一覧
    static std::vector<NetworkInterfaceInfo> GetNetworkInterfaces();
    // デフォルトゲートウェイ付きインターフェースの FriendlyName を返す
    static std::wstring GetDefaultGatewayInterfaceName();
};
