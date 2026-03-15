#pragma once
#include <cstdint>
#include <string>
#include <winrt/Windows.UI.h>

namespace DanmakuFrostMasterXx
{
    enum class DanmakuMode : int32_t
    {
        Unknown         = 0,
        Rolling         = 1,
        Bottom          = 4,
        Top             = 5,
        ReverseRolling  = 6,
        Advanced        = 7,
        Subtitle        = 9,
    };

    enum class DanmakuPool : int32_t
    {
        Normal   = 0,
        Subtitle = 1,
        Special  = 2,
    };

    enum class DanmakuAlignmentMode : int32_t
    {
        Default      = 0,
        LowerLeft    = 1,
        LowerCenter  = 2,
        LowerRight   = 3,
        MiddleLeft   = 4,
        MiddleCenter = 5,
        MiddleRight  = 6,
        UpperLeft    = 7,
        UpperCenter  = 8,
        UpperRight   = 9,
    };

    struct DanmakuItem
    {
        static constexpr float DefaultBaseFontSize = 22.0f;

        // Used to sort danmaku with the same StartMs
        uint64_t Id                 = 0;
        uint32_t StartMs            = 0;
        bool HasBorder              = false;
        bool HasOutline             = true;
        bool AllowDensityControl    = true;
        bool IsRealtime             = false;
        float BaseFontSize          = DefaultBaseFontSize;
        float OutlineSize           = 2.0f;
        std::wstring FontFamilyName;
        std::wstring Text;
        // -1 = null (use global), 0 = false, 1 = true
        int32_t IsBold              = -1;
        DanmakuMode Mode            = DanmakuMode::Unknown;
        winrt::Windows::UI::Color TextColor    = { 0xFF, 0xFF, 0xFF, 0xFF }; // White
        winrt::Windows::UI::Color OutlineColor = { 0xFF, 0x00, 0x00, 0x00 }; // Black

        // --- Advanced mode fields ---
        float StartX = 0.0f;
        float StartY = 0.0f;
        float EndX   = 0.0f;
        float EndY   = 0.0f;

        int32_t MarginLeft   = 0;
        int32_t MarginRight  = 0;
        int32_t MarginBottom = 0;

        DanmakuAlignmentMode AlignmentMode = DanmakuAlignmentMode::Default;
        DanmakuAlignmentMode AnchorMode    = DanmakuAlignmentMode::UpperLeft;

        uint8_t StartAlpha = 0xFF;
        uint8_t EndAlpha   = 0xFF;

        uint64_t DurationMs            = 0;
        uint64_t TranslationDurationMs = 0;
        uint64_t TranslationDelayMs    = 0;
        uint64_t AlphaDurationMs       = 0;
        uint64_t AlphaDelayMs          = 0;

        float RotateZ = 0.0f; // Degree
        float RotateY = 0.0f; // Degree

        bool KeepDefinedFontSize = false;
    };

    namespace DanmakuDefaultLayerDef
    {
        constexpr uint32_t DefaultLayerId        = 0;
        constexpr uint32_t RollingLayerId        = 0;
        constexpr uint32_t ReverseRollingLayerId = 1;
        constexpr uint32_t TopLayerId            = 2;
        constexpr uint32_t BottomLayerId         = 3;
        constexpr uint32_t AdvancedLayerId       = 4;
        constexpr uint32_t SubtitleLayerId       = 5;
        constexpr uint32_t DefaultLayerCount     = 6;
    }
}
