#include "item_dat_loader.h"
#include "packet_crypt.h"
#include <windows.h>
#include <fstream>
#include <algorithm>
#include <regex>
#include <sstream>

// Shift_JIS → wstring 変換
static std::wstring ShiftJisToWide(const char* sjis, int len) {
    if (len <= 0) return L"";
    int wlen = MultiByteToWideChar(932, 0, sjis, len, NULL, 0);
    if (wlen <= 0) return L"";
    std::wstring ws(wlen, L'\0');
    MultiByteToWideChar(932, 0, sjis, len, ws.data(), wlen);
    return ws;
}

std::wstring ItemDatLoader::ReadNullTerminatedShiftJis(const uint8_t* buf, int maxLen) {
    int nullIdx = -1;
    for (int i = 0; i < maxLen; i++) {
        if (buf[i] == 0) { nullIdx = i; break; }
    }
    int len = (nullIdx >= 0) ? nullIdx : maxLen;
    return ShiftJisToWide(reinterpret_cast<const char*>(buf), len);
}

std::wstring ItemDatLoader::StripColorTags(const std::wstring& text) {
    // <c:xxx> と <n> を除去
    std::wregex pat(LR"(<c:[^>]+>|<n>)");
    return std::regex_replace(text, pat, L"");
}

std::wstring ItemDatLoader::GetRedStonePathFromRegistry() {
    HKEY hKey = NULL;
    // Red Stone for Japan
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\L&K Logic Korea\\Red Stone for Japan", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH] = {};
        DWORD sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"path", NULL, NULL, reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS && buf[0]) {
            RegCloseKey(hKey);
            return buf;
        }
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"Excute Folder", NULL, NULL, reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS && buf[0]) {
            RegCloseKey(hKey);
            return buf;
        }
        RegCloseKey(hKey);
    }
    // Portable
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\L&K Logic Korea\\Red Stone Portable for Japan", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH] = {};
        DWORD sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"path", NULL, NULL, reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS && buf[0]) {
            RegCloseKey(hKey);
            return buf;
        }
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"Excute Folder", NULL, NULL, reinterpret_cast<BYTE*>(buf), &sz) == ERROR_SUCCESS && buf[0]) {
            RegCloseKey(hKey);
            return buf;
        }
        RegCloseKey(hKey);
    }
    return L"";
}

std::wstring ItemDatLoader::GetItemDatPathFromRegistry() {
    auto base = GetRedStonePathFromRegistry();
    if (base.empty()) return L"";
    auto path = base + L"\\Data\\Scenario\\Red Stone\\item.dat";
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) ? path : L"";
}

bool ItemDatLoader::LoadItemDat(const std::wstring& path) {
    auto p = path.empty() ? GetItemDatPathFromRegistry() : path;
    if (p.empty()) return false;

    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;

    if (isLoaded_ && loadedPath_ == p) return true;

    items_.clear();
    options_.clear();
    isLoaded_ = false;
    loadedPath_.clear();

    // Read file
    HANDLE hFile = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize < 12) { CloseHandle(hFile); return false; }

    std::vector<uint8_t> data(fileSize);
    DWORD bytesRead = 0;
    ReadFile(hFile, data.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    if (bytesRead < 12) return false;

    // Header
    int rawKey = *reinterpret_cast<int32_t*>(data.data());
    int key = PacketCrypt::GenerateScenarioDecodeKey(rawKey);

    // Decrypt header (offset 4-11, 8 bytes)
    auto decHeader = PacketCrypt::DecodeScenarioBuffer(data.data() + 4, 8, key);
    int libItemLength = *reinterpret_cast<int32_t*>(decHeader.data());

    int libItemsBytes = libItemLength * ITEM_BASE_INFO_SIZE;

    // Items block (offset 12)
    if (libItemLength > 0) {
        int available = static_cast<int>(data.size()) - 12;
        int readSize = (std::min)(libItemsBytes, available);
        if (readSize > 0) {
            auto decItems = PacketCrypt::DecodeScenarioBuffer(data.data() + 12, readSize, key);
            ParseItems(decItems);
        }
    }

    // Options block
    int nextPos = 12 + (std::max)(0, libItemsBytes);
    if (static_cast<int>(data.size()) >= nextPos + 4) {
        auto decOptLen = PacketCrypt::DecodeScenarioBuffer(data.data() + nextPos, 4, key);
        int optionCount = *reinterpret_cast<int32_t*>(decOptLen.data());
        if (optionCount > 0 && optionCount < 10000) {
            ParseOptions(data, nextPos + 4, optionCount, key);
        }
    }

    if (!items_.empty() || !options_.empty()) {
        isLoaded_ = true;
        loadedPath_ = p;
        return true;
    }
    return false;
}

void ItemDatLoader::ParseItems(const std::vector<uint8_t>& decItems) {
    int recCount = static_cast<int>(decItems.size()) / ITEM_BASE_INFO_SIZE;

    for (int i = 0; i < recCount; i++) {
        int off = i * ITEM_BASE_INFO_SIZE;
        if (off + 4 > static_cast<int>(decItems.size())) break;

        int idVal = *reinterpret_cast<const int32_t*>(decItems.data() + off);

        // Name: offset +4, 0x40 bytes
        if (off + 4 + 0x40 > static_cast<int>(decItems.size())) break;
        auto name = ReadNullTerminatedShiftJis(decItems.data() + off + 4, 0x40);
        name = StripColorTags(name);

        // Type: offset +76, 2 bytes unsigned short
        int typeVal = -1;
        if (off + 78 <= static_cast<int>(decItems.size())) {
            typeVal = *reinterpret_cast<const uint16_t*>(decItems.data() + off + 76);
        }

        // BasePrice: offset +96, 4 bytes
        int basePrice = 0;
        if (off + 100 <= static_cast<int>(decItems.size())) {
            basePrice = *reinterpret_cast<const int32_t*>(decItems.data() + off + 96);
        }

        items_[idVal] = ItemInfo{idVal, name, typeVal, basePrice};
    }
}

void ItemDatLoader::ParseOptions(const std::vector<uint8_t>& data, int opsOffset, int optionCount, int key) {
    std::vector<uint8_t> opsBytes;
    int pos = opsOffset;

    for (int i = 0; i < optionCount; i++) {
        if (pos >= static_cast<int>(data.size())) break;
        int remaining = static_cast<int>(data.size()) - pos;
        int chunkSize = (std::min)(OP_BASE_INFO_SIZE, remaining);
        if (chunkSize <= 0) break;

        auto dec = PacketCrypt::DecodeScenarioBuffer(data.data() + pos, chunkSize, key);
        opsBytes.insert(opsBytes.end(), dec.begin(), dec.end());
        pos += chunkSize;
    }

    if (static_cast<int>(opsBytes.size()) < OP_BASE_INFO_SIZE) return;

    int recCount = static_cast<int>(opsBytes.size()) / OP_BASE_INFO_SIZE;
    for (int i = 0; i < recCount; i++) {
        int off = i * OP_BASE_INFO_SIZE;
        if (off + 2 > static_cast<int>(opsBytes.size())) break;

        int idx = *reinterpret_cast<const uint16_t*>(opsBytes.data() + off);

        // Name1: offset 16, len 20
        std::wstring name1;
        if (off + 16 + 20 <= static_cast<int>(opsBytes.size())) {
            name1 = ReadNullTerminatedShiftJis(opsBytes.data() + off + 16, 20);
            name1 = StripColorTags(name1);
        }

        // Name2: offset 36, len 20
        std::wstring name2;
        if (off + 36 + 20 <= static_cast<int>(opsBytes.size())) {
            name2 = ReadNullTerminatedShiftJis(opsBytes.data() + off + 36, 20);
            name2 = StripColorTags(name2);
        }

        std::wstring finalName = !name1.empty() ? name1 : !name2.empty() ? name2 : std::to_wstring(idx);

        if (options_.find(idx) == options_.end()) {
            options_[idx] = OptionInfo{idx, finalName};
        }
    }
}

bool ItemDatLoader::Reload(const std::wstring& path) {
    auto p = path.empty() ? loadedPath_ : path;
    isLoaded_ = false;
    loadedPath_.clear();
    items_.clear();
    options_.clear();
    return !p.empty() ? LoadItemDat(p) : false;
}

const ItemInfo* ItemDatLoader::GetItemInfo(int itemId) const {
    auto it = items_.find(itemId);
    return (it != items_.end()) ? &it->second : nullptr;
}

std::wstring ItemDatLoader::GetItemName(int itemId) const {
    auto info = GetItemInfo(itemId);
    return info ? info->name : (L"serial+" + std::to_wstring(itemId));
}

const OptionInfo* ItemDatLoader::GetOptionInfo(int optionId) const {
    auto it = options_.find(optionId);
    return (it != options_.end()) ? &it->second : nullptr;
}

std::wstring ItemDatLoader::GetOptionName(int optionId) const {
    auto info = GetOptionInfo(optionId);
    return info ? info->name : L"";
}

std::vector<ItemInfo> ItemDatLoader::GetAllBlocksOrdered() const {
    // C# 版と同様に item.dat を再読み込みし、全ブロックをファイル内出現順で返す
    // items_ (unordered_map) は重複IDを上書きするため使わない
    std::vector<ItemInfo> result;

    auto p = loadedPath_.empty() ? GetItemDatPathFromRegistry() : loadedPath_;
    if (p.empty()) return result;

    HANDLE hFile = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return result;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize < 12) { CloseHandle(hFile); return result; }

    std::vector<uint8_t> data(fileSize);
    DWORD bytesRead = 0;
    ReadFile(hFile, data.data(), fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    if (bytesRead < 12) return result;

    int rawKey = *reinterpret_cast<int32_t*>(data.data());
    int key = PacketCrypt::GenerateScenarioDecodeKey(rawKey);

    auto decHeader = PacketCrypt::DecodeScenarioBuffer(data.data() + 4, 8, key);
    int libItemLength = *reinterpret_cast<int32_t*>(decHeader.data());

    int libItemsBytes = libItemLength * ITEM_BASE_INFO_SIZE;
    int available = static_cast<int>(data.size()) - 12;
    int readSize = (std::min)(libItemsBytes, (std::max)(0, available));
    if (readSize <= 0) return result;

    auto decItems = PacketCrypt::DecodeScenarioBuffer(data.data() + 12, readSize, key);
    int recCount = static_cast<int>(decItems.size()) / ITEM_BASE_INFO_SIZE;
    result.reserve(recCount);

    for (int i = 0; i < recCount; i++) {
        int off = i * ITEM_BASE_INFO_SIZE;
        if (off + 4 > static_cast<int>(decItems.size())) break;

        int idVal = *reinterpret_cast<const int32_t*>(decItems.data() + off);

        std::wstring name = L"serial+" + std::to_wstring(i);
        if (off + 4 + 0x40 <= static_cast<int>(decItems.size())) {
            auto read = ReadNullTerminatedShiftJis(decItems.data() + off + 4, 0x40);
            read = StripColorTags(read);
            if (!read.empty()) name = read;
        }

        int typeVal = -1;
        if (off + 78 <= static_cast<int>(decItems.size())) {
            typeVal = *reinterpret_cast<const uint16_t*>(decItems.data() + off + 76);
        }

        int basePrice = 0;
        if (off + 100 <= static_cast<int>(decItems.size())) {
            basePrice = *reinterpret_cast<const int32_t*>(decItems.data() + off + 96);
        }

        result.push_back(ItemInfo{idVal, name, typeVal, basePrice});
    }

    return result;
}
