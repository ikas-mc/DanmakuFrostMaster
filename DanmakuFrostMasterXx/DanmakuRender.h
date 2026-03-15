#pragma once
#include "DanmakuTypes.h"
#include "DanmakuYSlotManager.h"
#include "DanmakuLogger.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.Graphics.Canvas.h>
#include <winrt/Microsoft.Graphics.Canvas.Effects.h>
#include <winrt/Microsoft.Graphics.Canvas.Geometry.h>
#include <winrt/Microsoft.Graphics.Canvas.Text.h>
#include <winrt/Microsoft.Graphics.Canvas.UI.h>
#include <winrt/Microsoft.Graphics.Canvas.UI.Xaml.h>

namespace DanmakuFrostMasterXx
{
    class DanmakuRender
    {
    public:
        explicit DanmakuRender(
            winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasAnimatedControl const& canvas);
        ~DanmakuRender() noexcept;

        DanmakuRender(const DanmakuRender&) = delete;
        DanmakuRender& operator=(const DanmakuRender&) = delete;

        bool DebugMode = false;
        float CanvasWidth  = 0.0f;
        float CanvasHeight = 0.0f;

        void SetAutoControlDensity(bool value) noexcept;
        void SetRollingDensity(int32_t value) noexcept;
        void SetRollingAreaRatio(int32_t value);
        void SetRollingSpeed(int32_t value) noexcept;
        void SetOpacity(double value) noexcept;
        void SetIsTextBold(bool value) noexcept;
        void SetDanmakuFontSizeOffset(int32_t value) noexcept;   // 1-5
        void SetSubtitleFontSizeOffset(int32_t value) noexcept;  // 1-5
        void SetDefaultFontFamilyName(const std::wstring& value);
        void SetBorderColor(winrt::Windows::UI::Color color) noexcept;
        void SetNoOverlapSubtitle(bool value) noexcept;

        void RenderDanmakuItem(uint32_t layerId, DanmakuItem const& danmakuItem);

        void SetRenderState(bool renderDanmaku, bool renderSubtitle);
        void SetLayerRenderState(uint32_t layerId, bool render);
        void SetSubtitleLayer(uint32_t layerId);
        void ClearLayer(uint32_t layerId);
        void Clear();

        void Start();
        void Pause();
        void Stop();
        void Close() noexcept;

    private:
        // --- DanmakuRenderItem (internal) ---
        struct DanmakuRenderItem
        {
            uint32_t Id                  = 0;
            bool HasBorder               = false;
            bool HasOutline              = true;
            bool AllowDensityControl     = true;
            float FontSize               = DanmakuItem::DefaultBaseFontSize;
            float OutlineSize            = 2.0f;
            std::wstring FontFamilyName;
            std::wstring Text;
            int32_t IsBold               = -1;  // -1=null, 0=false, 1=true
            DanmakuMode Mode             = DanmakuMode::Unknown;
            winrt::Windows::UI::Color TextColor    = {};
            winrt::Windows::UI::Color OutlineColor = {};

            bool     IsFirstRenderTimeSet  = false;
            winrt::Windows::Foundation::TimeSpan FirstRenderTime{};

            winrt::Microsoft::Graphics::Canvas::CanvasRenderTarget RenderTarget{ nullptr };
            winrt::Microsoft::Graphics::Canvas::Effects::Transform3DEffect TransformEffect{ nullptr };
            winrt::Windows::Foundation::Rect SourceRect{};

            float Width              = 0.0f;
            float Height             = 0.0f;
            float X                  = 0.0f;
            float Y                  = 0.0f;
            bool  NeedToReleaseYSlot = false;

            // Advanced mode
            float DefinedStartX = 0.0f, DefinedStartY = 0.0f;
            float DefinedEndX   = 0.0f, DefinedEndY   = 0.0f;
            int32_t MarginLeft  = 0, MarginRight = 0, MarginBottom = 0;
            DanmakuAlignmentMode AlignmentMode = DanmakuAlignmentMode::Default;
            DanmakuAlignmentMode AnchorMode    = DanmakuAlignmentMode::UpperLeft;
            uint8_t DefinedStartAlpha = 0xFF, DefinedEndAlpha = 0xFF;
            uint64_t DefinedDurationMs = 0, DefinedTranslationDurationMs = 0;
            uint64_t DefinedTranslationDelayMs = 0;
            uint64_t DefinedAlphaDurationMs = 0, DefinedAlphaDelayMs = 0;
            float DefinedRotateZ = 0.0f, DefinedRotateY = 0.0f;
            float StartX = 0.0f, StartY = 0.0f;
            float EndX   = 0.0f, EndY   = 0.0f;
            float TranslationSpeedX = 0.0f, TranslationSpeedY = 0.0f;
            uint8_t Alpha = 0xFF;

            explicit DanmakuRenderItem(DanmakuItem const& item) noexcept;
            void Close() noexcept;
            ~DanmakuRenderItem() noexcept { Close(); }

            DanmakuRenderItem(DanmakuRenderItem&&) noexcept = default;
            DanmakuRenderItem& operator=(DanmakuRenderItem&&) noexcept = default;
            DanmakuRenderItem(const DanmakuRenderItem&) = delete;
            DanmakuRenderItem& operator=(const DanmakuRenderItem&) = delete;

        private:
            static std::atomic<uint32_t> s_nextId;
            static uint32_t GetNextId() noexcept;
        };

        // --- RenderLayer (internal) ---
        struct RenderLayer
        {
            uint32_t LayerId = 0;
            std::vector<DanmakuRenderItem> RenderList;
            mutable std::mutex             RenderListMutex;
            DanmakuYSlotManager            YSlotManager{ 0 };
            bool RequireStrictRenderOrder  = false;
            bool IsEnabled                 = true;
            bool IsSubtitleLayer           = false;

            explicit RenderLayer(uint32_t layerId, bool requireStrict) noexcept
                : LayerId(layerId), RequireStrictRenderOrder(requireStrict) {}

            // Not copyable
            RenderLayer(const RenderLayer&) = delete;
            RenderLayer& operator=(const RenderLayer&) = delete;

            void UpdateYSlotManagerLength(uint32_t newLength, float rollingAreaRatio);
            void Clear() noexcept;
            void SetSubtitleLayer(bool isSub) noexcept { IsSubtitleLayer = isSub; }
        };

        // --- Canvas and device ---
        winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasAnimatedControl m_canvas{ nullptr };
        winrt::Microsoft::Graphics::Canvas::CanvasDevice                    m_device{ nullptr };
        mutable std::mutex m_deviceMutex;

        // --- Event tokens ---
        winrt::event_token m_sizeChangedToken{};
        winrt::event_token m_createResourcesToken{};
        winrt::event_token m_updateToken{};
        winrt::event_token m_drawToken{};

        // --- Layers ---
        static constexpr uint32_t LayerCount = DanmakuDefaultLayerDef::DefaultLayerCount;
        std::unique_ptr<RenderLayer> m_renderLayers[LayerCount];

        // --- Settings (atomic for multi-thread access) ---
        std::atomic<bool>    m_isStopped{ false };
        std::atomic<bool>    m_isDanmakuEnabled{ true };
        std::atomic<bool>    m_isSubtitleEnabled{ true };
        std::atomic<bool>    m_autoControlDensity{ true };
        std::atomic<bool>    m_textBold{ true };
        std::atomic<bool>    m_noOverlapSubtitle{ false };
        std::atomic<int32_t> m_maxDanmakuSize{ 0 };
        std::atomic<int32_t> m_rollingDensity{ -1 };
        std::atomic<int32_t> m_danmakuFontSizeOffset{ 3 };    // Normal=3
        std::atomic<int32_t> m_subtitleFontSizeOffset{ 3 };
        std::atomic<float>   m_rollingAreaRatio{ 0.8f };
        std::atomic<float>   m_rollingSpeed{ 0.1f };

        double             m_textOpacity    = 1.0;
        winrt::Windows::UI::Color m_borderColor = { 0xFF, 0x00, 0x00, 0xFF }; // Blue
        std::wstring       m_defaultFontFamilyName = L"Microsoft YaHei";
        mutable std::mutex m_settingsMutex;  // protects non-atomic settings

        float m_dpi             = 96.0f;
        double m_appMemoryLimitMb = 0.0;

        // --- Constants ---
        static constexpr float Standard_Canvas_Width          = 800.0f;
        static constexpr float Default_Rolling_Speed          = 0.1f;
        static constexpr float Default_BottomAndTop_Duration_Ms = 3800.0f;
        static constexpr float Subtitle_StartY                = 24.0f;
        static constexpr const wchar_t* Default_Font_Family_Name = L"Microsoft YaHei";

        // --- Event handlers ---
        void OnSizeChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Windows::UI::Xaml::SizeChangedEventArgs const& args);

        void OnCreateResources(
            winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasAnimatedControl const& sender,
            winrt::Microsoft::Graphics::Canvas::UI::CanvasCreateResourcesEventArgs const& args);

        void OnUpdate(
            winrt::Microsoft::Graphics::Canvas::UI::Xaml::ICanvasAnimatedControl const& sender,
            winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasAnimatedUpdateEventArgs const& args);

        void OnDraw(
            winrt::Microsoft::Graphics::Canvas::UI::Xaml::ICanvasAnimatedControl const& sender,
            winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasAnimatedDrawEventArgs const& args);

        // --- Helpers ---
        static float DegreeToRadian(float degree) noexcept;
        static float AdjustRollingSpeedByWidth(float speed, float width) noexcept;
        static float TimeSpanToMs(winrt::Windows::Foundation::TimeSpan ts) noexcept;
    };
}
