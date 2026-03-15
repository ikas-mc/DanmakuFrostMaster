#pragma once
#include "DanmakuTypes.h"
#include <vector>
#include <string>

namespace DanmakuFrostMasterXx
{
    struct BilibiliParseResult
    {
        std::vector<DanmakuItem> Items;
        uint32_t TotalCount    = 0;
        uint32_t FilteredCount = 0;
        uint32_t MergedCount   = 0;
    };

    class BilibiliXmlParser
    {
    public:
        // Parse Bilibili XML danmaku string into a sorted list.
        // filterList: optional list of regex strings to filter danmaku text.
        static BilibiliParseResult ParseXml(
            const std::wstring& xmlStr,
            const std::vector<std::wstring>& filterList,
            bool mergeDuplicate);

        // Parse Bilibili subtitle JSON string ({"body":[...]}) into a sorted list.
        static std::vector<DanmakuItem> ParseSubtitleJson(const std::wstring& jsonStr);

    private:
        // Returns false if the item cannot be parsed.
        static bool ParseDanmakuItem(
            const std::wstring& tagStr,
            const std::wstring& content,
            bool isNewFormat,
            DanmakuItem& outItem);

        static winrt::Windows::UI::Color ParseColor(uint32_t colorValue) noexcept;

        // Replaces HTML entities and "\n" escape sequences.
        static std::wstring DecodeContent(const std::wstring& raw);

        static void SortItems(std::vector<DanmakuItem>& list);
        static void MergeSort(std::vector<DanmakuItem>& list, int p, int r);
        static void MergeArray(std::vector<DanmakuItem>& list, int p, int mid, int r);

        // Split wstring by single char delimiter
        static std::vector<std::wstring> SplitWStr(const std::wstring& s, wchar_t delim);

        // Try to parse float from wstring; returns def on failure
        static float TryParseFloat(const std::wstring& s, float def = 0.0f) noexcept;

        // Try to parse int from wstring; returns def on failure
        static int32_t TryParseInt(const std::wstring& s, int32_t def = 0) noexcept;

        // Try to parse uint64 from wstring; returns def on failure
        static uint64_t TryParseUInt64(const std::wstring& s, uint64_t def = 0) noexcept;

        // Extract string representation from a WinRT IJsonValue (for array indexing)
        static std::wstring JsonValueToWString(
            winrt::Windows::Data::Json::IJsonValue const& jv);
    };
}
