// dc_manager.h — DCManager: ドロップクリーンプロファイル管理
// C# DCManager.cs の C++ 移植
#pragma once

#include <string>
#include <vector>
#include <cstdint>

class AppSettings;

class DCManager {
public:
    explicit DCManager(const std::wstring& exeDir = L"");

    const std::wstring& ExeDir() const { return exeDir_; }
    const std::wstring& DcRoot() const { return dcRoot_; }

    void EnsureDcRootExists();
    std::vector<std::wstring> GetProfiles();
    std::wstring GetProfileDatPath(const std::wstring& profileName);
    std::vector<uint8_t> ReadDcBytes(const std::wstring& profileName);
    void WriteDcBytes(const std::wstring& profileName, const std::vector<uint8_t>& data);
    std::wstring LoadOrCreateOriginal(AppSettings& settings);
    bool CreateProfileFromExisting(const std::wstring& newProfileName, const std::wstring& sourceProfileName);

private:
    std::wstring exeDir_;
    std::wstring dcRoot_;
};
