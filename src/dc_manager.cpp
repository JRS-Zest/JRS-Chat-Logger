// dc_manager.cpp — DCManager: ドロップクリーンプロファイル管理
// C# DCManager.cs の C++ 移植
#include "dc_manager.h"
#include "app_settings.h"

#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

static std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    fs::path p(path);
    return p.parent_path().wstring();
}

DCManager::DCManager(const std::wstring& exeDir)
    : exeDir_(exeDir.empty() ? GetModuleDir() : exeDir)
    , dcRoot_((fs::path(exeDir_) / L"DC").wstring())
{
}

void DCManager::EnsureDcRootExists() {
    fs::create_directories(dcRoot_);
}

std::vector<std::wstring> DCManager::GetProfiles() {
    std::vector<std::wstring> result;
    if (!fs::exists(dcRoot_)) return result;
    for (const auto& entry : fs::directory_iterator(dcRoot_)) {
        if (!entry.is_directory()) continue;
        auto dcDat = entry.path() / L"DC.dat";
        if (fs::exists(dcDat)) {
            result.push_back(entry.path().filename().wstring());
        }
    }
    return result;
}

std::wstring DCManager::GetProfileDatPath(const std::wstring& profileName) {
    return (fs::path(dcRoot_) / profileName / L"DC.dat").wstring();
}

std::vector<uint8_t> DCManager::ReadDcBytes(const std::wstring& profileName) {
    auto path = GetProfileDatPath(profileName);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(ifs), {});
}

void DCManager::WriteDcBytes(const std::wstring& profileName, const std::vector<uint8_t>& data) {
    auto profileDir = fs::path(dcRoot_) / profileName;
    fs::create_directories(profileDir);
    auto path = (profileDir / L"DC.dat").wstring();
    std::ofstream ofs(path, std::ios::binary);
    if (ofs.is_open()) {
        ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
}

std::wstring DCManager::LoadOrCreateOriginal(AppSettings& settings) {
    EnsureDcRootExists();
    auto profiles = GetProfiles();
    if (!profiles.empty()) {
        if (!settings.dcProfile.empty()) {
            for (const auto& p : profiles) {
                if (p == settings.dcProfile) return settings.dcProfile;
            }
        }
        return profiles[0];
    }

    // ゲーム側の DC.dat をコピー
    std::wstring src;
    if (!settings.redStoneDatPath.empty()) {
        auto maybe = (fs::path(settings.redStoneDatPath) / L"DC.dat").wstring();
        if (fs::exists(maybe)) src = maybe;
    }

    if (src.empty()) {
        wchar_t pf86[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, 0, pf86) == S_OK) {
            auto fallback = (fs::path(pf86) / L"GameON" / L"RED STONE" / L"DC.dat").wstring();
            if (fs::exists(fallback)) src = fallback;
        }
    }

    auto targetDir = fs::path(dcRoot_) / L"オリジナル";
    fs::create_directories(targetDir);
    auto targetPath = (targetDir / L"DC.dat").wstring();

    if (!src.empty() && fs::exists(src)) {
        try {
            fs::copy_file(src, targetPath, fs::copy_options::overwrite_existing);
        } catch (...) {
            // fallback: create empty file
            std::ofstream(targetPath, std::ios::binary).close();
        }
    } else {
        std::ofstream(targetPath, std::ios::binary).close();
    }

    return L"オリジナル";
}

bool DCManager::CreateProfileFromExisting(const std::wstring& newProfileName, const std::wstring& sourceProfileName) {
    auto profiles = GetProfiles();
    if (profiles.size() >= 10) return false;
    auto srcPath = GetProfileDatPath(sourceProfileName);
    if (!fs::exists(srcPath)) return false;
    auto dstDir = fs::path(dcRoot_) / newProfileName;
    fs::create_directories(dstDir);
    auto dstPath = (dstDir / L"DC.dat").wstring();
    try {
        fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing);
        return true;
    } catch (...) {
        return false;
    }
}
