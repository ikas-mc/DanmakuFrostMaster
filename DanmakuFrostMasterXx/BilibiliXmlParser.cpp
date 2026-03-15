#include "pch.h"
#include "BilibiliXmlParser.h"
#include "DanmakuLogger.h"
#include <unordered_map>
#include <sstream>
#include <regex>

namespace DanmakuFrostMasterXx
{
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    std::vector<std::wstring> BilibiliXmlParser::SplitWStr(
        const std::wstring& s, wchar_t delim)
    {
        std::vector<std::wstring> parts;
        std::wstring cur;
        for (wchar_t c : s)
        {
            if (c == delim)
            {
                parts.push_back(std::move(cur));
                cur.clear();
            }
            else
            {
                cur += c;
            }
        }
        parts.push_back(std::move(cur));
        return parts;
    }

    float BilibiliXmlParser::TryParseFloat(const std::wstring& s, float def) noexcept
    {
        if (s.empty()) return def;
        try { return std::stof(s); }
        catch (...) { return def; }
    }

    int32_t BilibiliXmlParser::TryParseInt(const std::wstring& s, int32_t def) noexcept
    {
        if (s.empty()) return def;
        try { return std::stoi(s); }
        catch (...) { return def; }
    }

    uint64_t BilibiliXmlParser::TryParseUInt64(const std::wstring& s, uint64_t def) noexcept
    {
        if (s.empty()) return def;
        try { return std::stoull(s); }
        catch (...) { return def; }
    }

    // Replaces HTML entities and newline escape sequences, trims whitespace.
    std::wstring BilibiliXmlParser::DecodeContent(const std::wstring& raw)
    {
        std::wstring s;
        s.reserve(raw.size());

        for (size_t i = 0; i < raw.size(); )
        {
            if (raw[i] == L'&')
            {
                // Try to match entity
                size_t semi = raw.find(L';', i + 1);
                if (semi != std::wstring::npos && semi - i <= 10)
                {
                    std::wstring entity = raw.substr(i + 1, semi - i - 1);
                    bool matched = true;
                    if      (entity == L"amp")  { s += L'&'; }
                    else if (entity == L"lt")   { s += L'<'; }
                    else if (entity == L"gt")   { s += L'>'; }
                    else if (entity == L"quot") { s += L'"'; }
                    else if (entity == L"apos") { s += L'\''; }
                    else if (!entity.empty() && entity[0] == L'#')
                    {
                        wchar_t ch = 0;
                        if (entity.size() > 1 && (entity[1] == L'x' || entity[1] == L'X'))
                        {
                            try { ch = static_cast<wchar_t>(std::stoul(entity.substr(2), nullptr, 16)); }
                            catch (...) { matched = false; }
                        }
                        else
                        {
                            try { ch = static_cast<wchar_t>(std::stoul(entity.substr(1))); }
                            catch (...) { matched = false; }
                        }
                        if (matched) s += ch;
                    }
                    else { matched = false; }

                    if (matched) { i = semi + 1; continue; }
                }
                s += raw[i++];
            }
            else
            {
                s += raw[i++];
            }
        }

        // Replace "/n" and "\\n" with real newline
        auto replaceAll = [](std::wstring& str, const std::wstring& from, const std::wstring& to)
        {
            size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::wstring::npos)
            {
                str.replace(pos, from.size(), to);
                pos += to.size();
            }
        };
        replaceAll(s, L"/n",  L"\n");
        replaceAll(s, L"\\n", L"\n");

        // Trim leading/trailing whitespace (not newlines)
        const std::wstring ws = L" \t\r";
        size_t start = s.find_first_not_of(ws);
        if (start == std::wstring::npos) return {};
        size_t end = s.find_last_not_of(ws);
        return s.substr(start, end - start + 1);
    }

    winrt::Windows::UI::Color BilibiliXmlParser::ParseColor(uint32_t colorValue) noexcept
    {
        colorValue &= 0x00FFFFFF;
        uint8_t b = static_cast<uint8_t>(colorValue & 0xFF);
        uint8_t g = static_cast<uint8_t>((colorValue >> 8) & 0xFF);
        uint8_t r = static_cast<uint8_t>((colorValue >> 16) & 0xFF);
        return winrt::Windows::UI::Color{ 0xFF, r, g, b };
    }

    // Convert a WinRT IJsonValue to its string content (not JSON repr).
    std::wstring BilibiliXmlParser::JsonValueToWString(
        winrt::Windows::Data::Json::IJsonValue const& jv)
    {
        switch (jv.ValueType())
        {
        case winrt::Windows::Data::Json::JsonValueType::String:
            return std::wstring(jv.GetString());
        case winrt::Windows::Data::Json::JsonValueType::Number:
        {
            // Use Stringify() to get the raw JSON number string (e.g. "0.5")
            return std::wstring(jv.as<winrt::Windows::Data::Json::JsonValue>().Stringify());
        }
        case winrt::Windows::Data::Json::JsonValueType::Boolean:
            return jv.GetBoolean() ? L"true" : L"false";
        case winrt::Windows::Data::Json::JsonValueType::Null:
            return {};
        default:
            return std::wstring(jv.as<winrt::Windows::Data::Json::JsonValue>().Stringify());
        }
    }

    // -----------------------------------------------------------------------
    // ParseXml
    // -----------------------------------------------------------------------

    BilibiliParseResult BilibiliXmlParser::ParseXml(
        const std::wstring& xmlStr,
        const std::vector<std::wstring>& filterList,
        bool mergeDuplicate)
    {
        BilibiliParseResult result;
        if (xmlStr.empty()) return result;

        const bool isNewFormat = (xmlStr.find(L"<oid>") != std::wstring::npos);

        // Duplicate tracking: text → list of {StartMs, Count}
        struct DupItem { uint32_t StartMs; uint32_t Count; };
        std::unordered_map<std::wstring, std::vector<DupItem>> dupMap;

        // Manual scan for <d p="...">...</d> patterns
        const std::wstring tagOpen   = L"<d p=\"";
        const std::wstring tagMid    = L"\">";
        const std::wstring tagClose  = L"</d>";

        size_t pos = 0;
        while (true)
        {
            size_t tagStart = xmlStr.find(tagOpen, pos);
            if (tagStart == std::wstring::npos) break;

            size_t pStart = tagStart + tagOpen.size();
            size_t pEnd   = xmlStr.find(tagMid, pStart);
            if (pEnd == std::wstring::npos) break;

            size_t contentStart = pEnd + tagMid.size();
            size_t contentEnd   = xmlStr.find(tagClose, contentStart);
            if (contentEnd == std::wstring::npos) break;

            pos = contentEnd + tagClose.size();
            result.TotalCount++;

            std::wstring tagStr     = xmlStr.substr(pStart, pEnd - pStart);
            std::wstring contentRaw = xmlStr.substr(contentStart, contentEnd - contentStart);

            if (tagStr.empty() || contentRaw.empty())
            {
                result.FilteredCount++;
                continue;
            }

            std::wstring contentStr = DecodeContent(contentRaw);
            if (contentStr.empty())
            {
                result.FilteredCount++;
                continue;
            }

            auto pArray = SplitWStr(tagStr, L',');

            DanmakuMode danmakuMode = DanmakuMode::Unknown;

            // Duplicate merging
            if (mergeDuplicate && pArray.size() >= 4)
            {
                int32_t modeIdx = isNewFormat ? 3 : 1;
                int32_t timeIdx = isNewFormat ? 2 : 0;
                int32_t mode    = TryParseInt(pArray[static_cast<size_t>(modeIdx)], -1);
                double  time    = 0.0;
                try { time = std::stod(pArray[static_cast<size_t>(timeIdx)]); }
                catch (...) { mode = -1; }

                if (mode >= 0)
                {
                    danmakuMode = static_cast<DanmakuMode>(mode);
                    if ((danmakuMode == DanmakuMode::Rolling
                        || danmakuMode == DanmakuMode::Top
                        || danmakuMode == DanmakuMode::Bottom)
                        && time >= 0.0)
                    {
                        uint32_t startMs = static_cast<uint32_t>(isNewFormat ? time : time * 1000.0);
                        auto& dupList    = dupMap[contentStr];
                        if (dupList.empty())
                        {
                            dupList.push_back({ startMs, 1u });
                        }
                        else
                        {
                            bool merged = false;
                            for (auto& dup : dupList)
                            {
                                int64_t diff = static_cast<int64_t>(startMs) - static_cast<int64_t>(dup.StartMs);
                                if (diff < 0) diff = -diff;
                                if (diff <= 20000)
                                {
                                    dup.Count++;
                                    merged = true;
                                    break;
                                }
                            }
                            if (merged)
                            {
                                result.MergedCount++;
                                continue;
                            }
                            dupList.push_back({ startMs, 1u });
                        }
                    }
                }
            }

            // Regex filter (skip Advanced / Subtitle)
            if (danmakuMode != DanmakuMode::Advanced
                && danmakuMode != DanmakuMode::Subtitle
                && !filterList.empty())
            {
                bool filtered = false;
                for (auto& pattern : filterList)
                {
                    try
                    {
                        std::wregex re(pattern, std::regex_constants::ECMAScript);
                        if (std::regex_search(contentStr, re))
                        {
                            DanmakuLogger::Log(std::wstring(L"Filtered danmaku: ") + contentStr);
                            filtered = true;
                            result.FilteredCount++;
                            break;
                        }
                    }
                    catch (...) {}
                }
                if (filtered) continue;
            }

            DanmakuItem item;
            if (ParseDanmakuItem(tagStr, contentStr, isNewFormat, item))
            {
                result.Items.push_back(std::move(item));
            }
            else
            {
                DanmakuLogger::Log(std::wstring(L"Failed to create danmaku: ") + contentStr);
            }
        }

        // Apply duplicate count suffix
        if (!dupMap.empty())
        {
            for (auto& item : result.Items)
            {
                if (item.Mode == DanmakuMode::Rolling
                    || item.Mode == DanmakuMode::Top
                    || item.Mode == DanmakuMode::Bottom)
                {
                    auto it = dupMap.find(item.Text);
                    if (it != dupMap.end())
                    {
                        for (auto& dup : it->second)
                        {
                            if (dup.Count > 1 && item.StartMs == dup.StartMs)
                            {
                                item.Text += L"\u00D7" + std::to_wstring(dup.Count);
                                break;
                            }
                        }
                    }
                }
            }
        }

        SortItems(result.Items);
        return result;
    }

    // -----------------------------------------------------------------------
    // ParseSubtitleJson
    // -----------------------------------------------------------------------

    std::vector<DanmakuItem> BilibiliXmlParser::ParseSubtitleJson(const std::wstring& jsonStr)
    {
        std::vector<DanmakuItem> list;
        if (jsonStr.empty()) return list;

        try
        {
            winrt::Windows::Data::Json::JsonObject jObj;
            if (!winrt::Windows::Data::Json::JsonObject::TryParse(jsonStr, jObj))
            {
                DanmakuLogger::Log(L"ParseSubtitleJson: failed to parse JSON object");
                return list;
            }

            if (!jObj.HasKey(L"body")) return list;

            auto bodyArray = jObj.GetNamedArray(L"body");
            for (auto const& jToken : bodyArray)
            {
                try
                {
                    if (jToken.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object)
                        continue;

                    auto obj    = jToken.GetObject();
                    double from = obj.GetNamedNumber(L"from") * 1000.0;
                    double to   = obj.GetNamedNumber(L"to")   * 1000.0;

                    if (to <= from) continue;

                    winrt::hstring hContent = obj.GetNamedString(L"content");
                    std::wstring content(hContent);
                    if (content.empty()) continue;

                    DanmakuItem item;
                    item.Mode                = DanmakuMode::Subtitle;
                    item.StartMs             = static_cast<uint32_t>(from);
                    item.DurationMs          = static_cast<uint64_t>(to - from);
                    item.Text                = content;
                    item.TextColor           = { 0xFF, 0xFF, 0xFF, 0xFF };
                    item.BaseFontSize        = DanmakuItem::DefaultBaseFontSize;
                    item.HasOutline          = false;
                    item.AllowDensityControl = false;

                    // Trim each sentence line
                    auto lines = SplitWStr(item.Text, L'\n');
                    for (auto& line : lines)
                    {
                        // Trim spaces
                        size_t s = line.find_first_not_of(L' ');
                        size_t e = line.find_last_not_of(L' ');
                        line = (s == std::wstring::npos) ? L"" : line.substr(s, e - s + 1);
                    }
                    item.Text.clear();
                    for (size_t i = 0; i < lines.size(); ++i)
                    {
                        if (i > 0) item.Text += L'\n';
                        item.Text += lines[i];
                    }

                    list.push_back(std::move(item));
                }
                catch (...)
                {
                    DanmakuLogger::Log(L"ParseSubtitleJson: failed to parse subtitle entry");
                }
            }
        }
        catch (winrt::hresult_error const& ex)
        {
            DanmakuLogger::Log(std::wstring(L"ParseSubtitleJson error: ") + ex.message().c_str());
        }

        SortItems(list);
        return list;
    }

    // -----------------------------------------------------------------------
    // ParseDanmakuItem
    // -----------------------------------------------------------------------

    bool BilibiliXmlParser::ParseDanmakuItem(
        const std::wstring& tagStr,
        const std::wstring& content,
        bool isNewFormat,
        DanmakuItem& outItem)
    {
        auto pArray = SplitWStr(tagStr, L',');
        if (pArray.size() < 8) return false;

        try
        {
            outItem.HasBorder = false;
            outItem.Text      = content;

            // ID
            outItem.Id = isNewFormat ? TryParseUInt64(pArray[0]) : 0ULL;

            // Color
            uint32_t colorVal = 0;
            try { colorVal = static_cast<uint32_t>(std::stoul(pArray[isNewFormat ? 5 : 3])); }
            catch (...) {}
            outItem.TextColor = ParseColor(colorVal);

            // StartMs
            double startMs = 0.0;
            try { startMs = isNewFormat
                ? std::stod(pArray[2])
                : std::stod(pArray[0]) * 1000.0; }
            catch (...) {}
            if (startMs < 0.0) startMs = 0.0;
            outItem.StartMs = static_cast<uint32_t>(startMs);

            // Mode
            int32_t mode = TryParseInt(pArray[isNewFormat ? 3 : 1], 0);
            switch (mode)
            {
            case static_cast<int32_t>(DanmakuMode::Rolling):
                outItem.Mode = DanmakuMode::Rolling; break;
            case static_cast<int32_t>(DanmakuMode::Bottom):
                outItem.Mode = DanmakuMode::Bottom; break;
            case static_cast<int32_t>(DanmakuMode::Top):
                outItem.Mode = DanmakuMode::Top; break;
            case static_cast<int32_t>(DanmakuMode::ReverseRolling):
                outItem.Mode = DanmakuMode::ReverseRolling; break;
            case static_cast<int32_t>(DanmakuMode::Advanced):
                outItem.Mode = DanmakuMode::Advanced; break;
            default:
                DanmakuLogger::Log(std::wstring(L"Skip unknown danmaku type: ")
                    + std::to_wstring(mode));
                return false;
            }

            // FontSize
            int32_t fontSize = TryParseInt(pArray[isNewFormat ? 4 : 2], 0);
            switch (outItem.Mode)
            {
            case DanmakuMode::Rolling:
            case DanmakuMode::Bottom:
            case DanmakuMode::Top:
            case DanmakuMode::ReverseRolling:
                fontSize -= (fontSize % 2 == 1) ? 3 : 2;
                break;
            case DanmakuMode::Advanced:
                fontSize += 4; // Experimental adjustment
                break;
            default:
                break;
            }
            if (fontSize < 2) fontSize = 2;
            outItem.BaseFontSize = static_cast<float>(fontSize);

            // Advanced mode
            if (outItem.Mode == DanmakuMode::Advanced)
            {
                if (content.empty() || content.front() != L'[' || content.back() != L']')
                    return false;

                outItem.AllowDensityControl = false;

                winrt::Windows::Data::Json::JsonArray jArray;
                if (!winrt::Windows::Data::Json::JsonArray::TryParse(
                    winrt::hstring(content), jArray))
                {
                    DanmakuLogger::Log(std::wstring(L"Failed to parse advanced JSON: ") + content);
                    return false;
                }

                uint32_t jLen = jArray.Size();
                if (jLen < 5) return false;

                // [4] Text
                std::wstring text = JsonValueToWString(jArray.GetAt(4));
                // HTML decode text and replace /n
                text = DecodeContent(text);
                if (text.empty()) return false;
                outItem.Text = std::move(text);

                // [0] StartX, [1] StartY
                outItem.StartX = static_cast<float>(jArray.GetAt(0).GetNumber());
                outItem.StartY = static_cast<float>(jArray.GetAt(1).GetNumber());
                outItem.EndX   = outItem.StartX;
                outItem.EndY   = outItem.StartY;

                // [2] Opacity "start" or "start-end"
                std::wstring opacityStr = JsonValueToWString(jArray.GetAt(2));
                {
                    auto parts = SplitWStr(opacityStr, L'-');
                    float startAlpha = std::max(TryParseFloat(parts[0]), 0.0f);
                    outItem.StartAlpha = static_cast<uint8_t>(startAlpha * 255.0f);
                    outItem.EndAlpha   = (parts.size() > 1)
                        ? static_cast<uint8_t>(std::max(TryParseFloat(parts[1]), 0.0f) * 255.0f)
                        : outItem.StartAlpha;
                }

                // [3] Duration (seconds → ms)
                outItem.DurationMs            = static_cast<uint64_t>(
                    static_cast<float>(jArray.GetAt(3).GetNumber()) * 1000.0f);
                outItem.TranslationDurationMs = outItem.DurationMs;
                outItem.TranslationDelayMs    = 0;
                outItem.AlphaDurationMs       = outItem.DurationMs;
                outItem.AlphaDelayMs          = 0;

                // [5] RotateZ, [6] RotateY
                if (jLen >= 7)
                {
                    outItem.RotateZ = static_cast<float>(jArray.GetAt(5).GetNumber());
                    outItem.RotateY = static_cast<float>(jArray.GetAt(6).GetNumber());
                }

                // [7] EndX, [8] EndY, [9] TranslationDuration, [10] TranslationDelay
                if (jLen >= 11)
                {
                    outItem.EndX = static_cast<float>(jArray.GetAt(7).GetNumber());
                    outItem.EndY = static_cast<float>(jArray.GetAt(8).GetNumber());

                    std::wstring tdStr = JsonValueToWString(jArray.GetAt(9));
                    if (!tdStr.empty())
                        outItem.TranslationDurationMs = static_cast<uint64_t>(TryParseFloat(tdStr));

                    std::wstring delayStr = JsonValueToWString(jArray.GetAt(10));
                    // "０" (fullwidth zero) compatibility
                    if (delayStr == L"\uFF10" || delayStr == L"0")
                        outItem.TranslationDelayMs = 0;
                    else if (!delayStr.empty())
                        outItem.TranslationDelayMs = static_cast<uint64_t>(TryParseFloat(delayStr));
                }

                outItem.HasOutline     = false;
                outItem.FontFamilyName = L"Consolas";
                outItem.KeepDefinedFontSize = true;
            }

            return true;
        }
        catch (...)
        {
            DanmakuLogger::Log(std::wstring(L"Failed to parse danmaku tag: ") + tagStr);
            return false;
        }
    }

    // -----------------------------------------------------------------------
    // Merge sort (stable, same logic as C# BilibiliDanmakuSorter)
    // -----------------------------------------------------------------------

    void BilibiliXmlParser::SortItems(std::vector<DanmakuItem>& list)
    {
        if (list.size() > 1)
            MergeSort(list, 0, static_cast<int>(list.size()) - 1);
    }

    void BilibiliXmlParser::MergeSort(std::vector<DanmakuItem>& list, int p, int r)
    {
        if (p < r)
        {
            int mid = (p + r) / 2;
            MergeSort(list, p, mid);
            MergeSort(list, mid + 1, r);
            MergeArray(list, p, mid, r);
        }
    }

    void BilibiliXmlParser::MergeArray(
        std::vector<DanmakuItem>& list, int p, int mid, int r)
    {
        std::vector<DanmakuItem> tmp;
        tmp.reserve(static_cast<size_t>(r - p + 1));

        int i = p, j = mid + 1;
        const int m = mid, n = r;

        while (i <= m && j <= n)
        {
            if (list[i].StartMs < list[j].StartMs)
            {
                tmp.push_back(std::move(list[i++]));
            }
            else if (list[i].StartMs > list[j].StartMs)
            {
                tmp.push_back(std::move(list[j++]));
            }
            else if (list[i].Mode == DanmakuMode::Advanced)
            {
                if (list[i].Id <= list[j].Id)
                    tmp.push_back(std::move(list[i++]));
                else
                    tmp.push_back(std::move(list[j++]));
            }
            else
            {
                tmp.push_back(std::move(list[i++]));
            }
        }
        while (i <= m) tmp.push_back(std::move(list[i++]));
        while (j <= n) tmp.push_back(std::move(list[j++]));

        for (int k = 0; k < r - p + 1; ++k)
            list[p + k] = std::move(tmp[k]);
    }

} 
