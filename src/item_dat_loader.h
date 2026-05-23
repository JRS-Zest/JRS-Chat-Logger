#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

// C# ItemDatLoader.cs の完全移植

struct ItemInfo {
    int id = 0;
    std::wstring name;
    int type = -1;
    int basePrice = 0;
};

struct OptionInfo {
    int id = 0;
    std::wstring name;
};

class ItemDatLoader {
public:
    bool IsLoaded() const { return isLoaded_; }
    int ItemCount() const { return static_cast<int>(items_.size()); }
    int OptionCount() const { return static_cast<int>(options_.size()); }

    // レジストリからパス取得
    static std::wstring GetRedStonePathFromRegistry();
    static std::wstring GetItemDatPathFromRegistry();

    // item.dat 読み込み
    bool LoadItemDat(const std::wstring& path = L"");
    bool Reload(const std::wstring& path = L"");

    // アイテム/オプション情報取得
    const ItemInfo* GetItemInfo(int itemId) const;
    std::wstring GetItemName(int itemId) const;
    const OptionInfo* GetOptionInfo(int optionId) const;
    std::wstring GetOptionName(int optionId) const;

    // 全ブロックをファイル内出現順で返す（DC設定用、重複ID含む）
    std::vector<ItemInfo> GetAllBlocksOrdered() const;

private:
    static constexpr int ITEM_BASE_INFO_SIZE = 426;
    static constexpr int OP_BASE_INFO_SIZE = 160;

    void ParseItems(const std::vector<uint8_t>& decItems);
    void ParseOptions(const std::vector<uint8_t>& data, int opsOffset, int optionCount, int key);

    static std::wstring ReadNullTerminatedShiftJis(const uint8_t* buf, int maxLen);
    static std::wstring StripColorTags(const std::wstring& text);

    std::unordered_map<int, ItemInfo> items_;
    std::unordered_map<int, OptionInfo> options_;
    bool isLoaded_ = false;
    std::wstring loadedPath_;
};
