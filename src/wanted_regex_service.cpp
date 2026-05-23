#include "wanted_regex_service.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <locale>
#include <codecvt>

static std::wstring GetExeDirW() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring p(path);
    auto pos = p.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? p.substr(0, pos) : L".";
}

WantedRegexService::WantedRegexService(const std::wstring& basePath) {
    auto base = basePath.empty() ? GetExeDirW() : basePath;
    filePath_ = base + L"\\keywords.txt";
}

void WantedRegexService::Load() {
    regexes_.clear();

    DWORD attr = GetFileAttributesW(filePath_.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        CreateSampleFile();
    }

    std::wifstream file(filePath_);
    if (!file.is_open()) return;
    file.imbue(std::locale(file.getloc(), new std::codecvt_utf8_utf16<wchar_t, 0x10FFFF, std::consume_header>));

    std::wstring line;
    while (std::getline(file, line)) {
        // Trim
        auto start = line.find_first_not_of(L" \t\r\n");
        if (start == std::wstring::npos) continue;
        auto trimmed = line.substr(start);
        auto end = trimmed.find_last_not_of(L" \t\r\n");
        if (end != std::wstring::npos) trimmed = trimmed.substr(0, end + 1);

        if (trimmed.empty() || trimmed[0] == L'/' || trimmed[0] == L'#') continue;

        try {
            regexes_.emplace_back(trimmed, boost::regex_constants::icase | boost::regex_constants::optimize);
        } catch (...) {
            // Invalid regex, skip
        }
    }
}

void WantedRegexService::CreateSampleFile() {
    std::wofstream file(filePath_);
    if (!file.is_open()) return;
    file.imbue(std::locale(file.getloc(), new std::codecvt_utf8_utf16<wchar_t, 0x10FFFF, std::generate_header>));

    file << L"/ 1行に1つの正規表現を記述してください。 / で始まる行はコメントとして無視されます。\n";
    file << L"\n";
    file << L"/ 運比率上昇Lv2と力比率上昇Lv2が付いたアイテムを探す例\n";
    file << L"(?=.*運比率上昇Lv2)(?=.*力比率上昇Lv2)\n";
    file << L"\n";
    file << L"/ バトルリングUltimate で命中補正無視が付いたものを探す例\n";
    file << L"命中補正無視.*バトルリングUltimate\n";
    file << L"\n";
    file << L"/ [知識比率上昇] または [力比率上昇] が付いたすべてのアイテムを探す例\n";
    file << L"(知識比率上昇|力比率上昇)\n";
}

bool WantedRegexService::IsNotificationTarget(const std::wstring& itemName, const std::vector<std::wstring>& options) const {
    if (regexes_.empty() || itemName.empty()) return false;

    std::wstring compound;
    for (auto& opt : options) {
        if (!compound.empty()) compound += L" ";
        compound += opt;
    }
    if (!compound.empty()) compound += L" ";
    compound += itemName;

    for (auto& re : regexes_) {
        try {
            if (boost::regex_search(compound, re)) return true;
        } catch (...) {}
    }
    return false;
}

RegexMatchResult WantedRegexService::GetMatchingCells(const std::wstring& itemName, const std::vector<std::wstring>& options) const {
    RegexMatchResult result;
    result.optionMatches.resize(options.size(), false);

    if (regexes_.empty()) return result;

    std::wstring compound;
    for (auto& opt : options) {
        if (!compound.empty()) compound += L" ";
        compound += opt;
    }
    if (!compound.empty()) compound += L" ";
    compound += itemName;

    for (auto& re : regexes_) {
        bool regexMatchesCompound = false;
        try { regexMatchesCompound = boost::regex_search(compound, re); } catch (...) {}

        // itemName alone
        bool thisItemMatch = false;
        try {
            if (!itemName.empty() && boost::regex_search(itemName, re)) {
                result.itemMatch = true;
                thisItemMatch = true;
                continue;
            }
        } catch (...) {}

        bool anyOptionMatched = false;
        for (size_t i = 0; i < options.size(); i++) {
            if (options[i].empty()) continue;
            try {
                if (boost::regex_search(options[i], re)) {
                    result.optionMatches[i] = true;
                    anyOptionMatched = true;
                }
            } catch (...) {}
        }

        // Compound match but no individual match → mark all options
        if (regexMatchesCompound && !thisItemMatch && !anyOptionMatched) {
            for (size_t i = 0; i < result.optionMatches.size(); i++)
                result.optionMatches[i] = true;
        }
    }

    return result;
}

std::wstring WantedRegexService::GetContent() const {
    std::wifstream ifs(filePath_);
    if (!ifs.is_open()) return L"";
    ifs.imbue(std::locale(ifs.getloc(), new std::codecvt_utf8_utf16<wchar_t, 0x10FFFF, std::consume_header>));
    std::wstringstream wss;
    wss << ifs.rdbuf();
    // EDIT コントロール用に \n → \r\n 変換
    std::wstring s = wss.str();
    std::wstring result;
    result.reserve(s.size() + s.size() / 4);
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == L'\n' && (i == 0 || s[i - 1] != L'\r'))
            result += L'\r';
        result += s[i];
    }
    return result;
}

bool WantedRegexService::SaveContent(const std::wstring& content) {
    try {
        std::wofstream ofs(filePath_);
        if (!ofs.is_open()) return false;
        ofs.imbue(std::locale(ofs.getloc(), new std::codecvt_utf8_utf16<wchar_t, 0x10FFFF, std::generate_header>));
        ofs << content;
        ofs.close();
        Load(); // 正規表現再読み込み
        return true;
    } catch (...) {
        return false;
    }
}
