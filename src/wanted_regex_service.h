#pragma once
#include <string>
#include <vector>
#include <boost/regex.hpp>

// C# WantedRegexService.cs の移植
// keywords.txt からの正規表現管理

struct RegexMatchResult {
    bool itemMatch = false;
    std::vector<bool> optionMatches;
};

class WantedRegexService {
public:
    WantedRegexService(const std::wstring& basePath = L"");
    void Load();
    void Reload() { Load(); }
    int Count() const { return static_cast<int>(regexes_.size()); }

    bool IsNotificationTarget(const std::wstring& itemName, const std::vector<std::wstring>& options) const;
    RegexMatchResult GetMatchingCells(const std::wstring& itemName, const std::vector<std::wstring>& options) const;

    // ダイアログ用: ファイル内容を文字列で取得/保存
    std::wstring GetContent() const;
    bool SaveContent(const std::wstring& content);

private:
    void CreateSampleFile();
    std::wstring filePath_;
    std::vector<boost::wregex> regexes_;
};
