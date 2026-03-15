#include "pch.h"
#include "DanmakuRender.h"
#include <DirectXMath.h>

namespace DanmakuFrostMasterXx
{
    // =========================================================================
    // DanmakuRenderItem implementation
    // =========================================================================

    std::atomic<uint32_t> DanmakuRender::DanmakuRenderItem::s_nextId{ 1 };

    uint32_t DanmakuRender::DanmakuRenderItem::GetNextId() noexcept
    {
        uint32_t id = s_nextId.fetch_add(1, std::memory_order_relaxed);
        if (id == 0) id = s_nextId.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

    DanmakuRender::DanmakuRenderItem::DanmakuRenderItem(DanmakuItem const& item) noexcept
    {
        Id                   = GetNextId();
        HasBorder            = item.HasBorder;
        HasOutline           = item.HasOutline;
        AllowDensityControl  = item.AllowDensityControl;
        FontSize             = item.BaseFontSize;
        OutlineSize          = item.OutlineSize;
        FontFamilyName       = item.FontFamilyName;
        Text                 = item.Text;
        IsBold               = item.IsBold;
        Mode                 = item.Mode;
        TextColor            = item.TextColor;
        OutlineColor         = item.OutlineColor;

        if (Mode == DanmakuMode::Top || Mode == DanmakuMode::Bottom || Mode == DanmakuMode::Advanced)
        {
            MarginBottom       = item.MarginBottom;
            DefinedDurationMs  = item.DurationMs;
        }

        if (Mode == DanmakuMode::Advanced)
        {
            DefinedStartX              = item.StartX;
            DefinedStartY              = item.StartY;
            DefinedEndX                = item.EndX;
            DefinedEndY                = item.EndY;
            MarginLeft                 = item.MarginLeft;
            MarginRight                = item.MarginRight;
            AlignmentMode              = item.AlignmentMode;
            AnchorMode                 = item.AnchorMode;
            DefinedStartAlpha          = item.StartAlpha;
            DefinedEndAlpha            = item.EndAlpha;
            TextColor.A                = 0xFF; // Always full opacity; Alpha controls rendering
            Alpha                      = DefinedStartAlpha;
            DefinedTranslationDurationMs = item.TranslationDurationMs;
            DefinedTranslationDelayMs  = item.TranslationDelayMs;
            DefinedAlphaDurationMs     = item.AlphaDurationMs;
            DefinedAlphaDelayMs        = item.AlphaDelayMs;
            DefinedRotateZ             = item.RotateZ;
            DefinedRotateY             = item.RotateY;
        }
        else if (Mode == DanmakuMode::Subtitle)
        {
            DefinedDurationMs = item.DurationMs;
        }
    }

    void DanmakuRender::DanmakuRenderItem::Close() noexcept
    {
        try
        {
            if (RenderTarget)  { RenderTarget.Close();  RenderTarget  = nullptr; }
            if (TransformEffect) { TransformEffect.Close(); TransformEffect = nullptr; }
        }
        catch (...) {}
    }

    // =========================================================================
    // RenderLayer implementation
    // =========================================================================

    void DanmakuRender::RenderLayer::UpdateYSlotManagerLength(
        uint32_t newLength, float rollingAreaRatio)
    {
        if (LayerId == DanmakuDefaultLayerDef::RollingLayerId
            || LayerId == DanmakuDefaultLayerDef::ReverseRollingLayerId)
        {
            YSlotManager.UpdateLength(static_cast<uint32_t>(newLength * rollingAreaRatio));
        }
        else if (LayerId == DanmakuDefaultLayerDef::TopLayerId)
        {
            YSlotManager.UpdateLength(static_cast<uint32_t>(newLength * 0.75f));
        }
        else if (LayerId == DanmakuDefaultLayerDef::BottomLayerId)
        {
            YSlotManager.UpdateLength(newLength / 2u);
        }
        else
        {
            YSlotManager.UpdateLength(newLength);
        }
    }

    void DanmakuRender::RenderLayer::Clear() noexcept
    {
        std::lock_guard<std::mutex> lock(RenderListMutex);
        for (auto& item : RenderList) item.Close();
        RenderList.clear();
        YSlotManager.Clear();
    }

    // =========================================================================
    // DanmakuRender constructor / destructor
    // =========================================================================

    DanmakuRender::DanmakuRender(
        winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasAnimatedControl const& canvas)
    {
        if (!canvas) throw std::invalid_argument("canvas is null");

        m_canvas = canvas;
        m_canvas.IsFixedTimeStep(false);
        m_dpi         = m_canvas.Dpi();
        CanvasWidth   = static_cast<float>(m_canvas.ActualWidth());
        CanvasHeight  = static_cast<float>(m_canvas.ActualHeight());

        // Subscribe to events
        m_sizeChangedToken = m_canvas.SizeChanged(
            { this, &DanmakuRender::OnSizeChanged });

        m_createResourcesToken = m_canvas.CreateResources(
            { this, &DanmakuRender::OnCreateResources });

        m_updateToken = m_canvas.Update(
            { this, &DanmakuRender::OnUpdate });

        m_drawToken = m_canvas.Draw(
            { this, &DanmakuRender::OnDraw });

        m_canvas.Paused(false);

        // Create layers
        for (uint32_t i = 0; i < LayerCount; ++i)
        {
            bool strict = (i == DanmakuDefaultLayerDef::AdvancedLayerId
                        || i == DanmakuDefaultLayerDef::SubtitleLayerId);
            m_renderLayers[i] = std::make_unique<RenderLayer>(i, strict);
            if (CanvasHeight >= 1.0f)
            {
                m_renderLayers[i]->UpdateYSlotManagerLength(
                    static_cast<uint32_t>(CanvasHeight),
                    m_rollingAreaRatio.load());
            }
        }

        m_appMemoryLimitMb = static_cast<double>(
            winrt::Windows::System::MemoryManager::AppMemoryUsageLimit()) / (1024.0 * 1024.0);

        DanmakuLogger::Log(L"DanmakuRender is created");
    }

    DanmakuRender::~DanmakuRender() noexcept
    {
        Close();
    }

    // =========================================================================
    // Settings setters
    // =========================================================================

    void DanmakuRender::SetAutoControlDensity(bool value) noexcept  { m_autoControlDensity = value; }
    void DanmakuRender::SetRollingDensity(int32_t value) noexcept   { m_rollingDensity = value; }
    void DanmakuRender::SetNoOverlapSubtitle(bool value) noexcept   { m_noOverlapSubtitle = value; }

    void DanmakuRender::SetRollingAreaRatio(int32_t value)
    {
        if (value > 0 && value <= 10)
        {
            m_rollingAreaRatio = static_cast<float>(value) / 10.0f;
            for (auto& layer : m_renderLayers)
                layer->UpdateYSlotManagerLength(
                    static_cast<uint32_t>(CanvasHeight), m_rollingAreaRatio.load());
        }
    }

    void DanmakuRender::SetRollingSpeed(int32_t value) noexcept
    {
        if (value >= 1 && value <= 10)
            m_rollingSpeed = value * 0.02f;
    }

    void DanmakuRender::SetOpacity(double value) noexcept
    {
        if (value > 0.0 && value <= 1.0)
        {
            std::lock_guard<std::mutex> lock(m_settingsMutex);
            m_textOpacity = value;
        }
    }

    void DanmakuRender::SetIsTextBold(bool value) noexcept { m_textBold = value; }

    void DanmakuRender::SetDanmakuFontSizeOffset(int32_t value) noexcept
    {
        if (value >= 1 && value <= 5) m_danmakuFontSizeOffset = value;
    }

    void DanmakuRender::SetSubtitleFontSizeOffset(int32_t value) noexcept
    {
        if (value >= 1 && value <= 5) m_subtitleFontSizeOffset = value;
    }

    void DanmakuRender::SetDefaultFontFamilyName(const std::wstring& value)
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        m_defaultFontFamilyName = value.empty() ? Default_Font_Family_Name : value;
    }

    void DanmakuRender::SetBorderColor(winrt::Windows::UI::Color color) noexcept
    {
        std::lock_guard<std::mutex> lock(m_settingsMutex);
        m_borderColor = color;
    }

    // =========================================================================
    // RenderDanmakuItem
    // =========================================================================

    void DanmakuRender::RenderDanmakuItem(uint32_t layerId, DanmakuItem const& danmakuItem)
    {
        if (layerId >= LayerCount)
            throw std::out_of_range("layerId exceeds max layer count");

        if (m_isStopped.load()) return;

        {
            std::lock_guard<std::mutex> lock(m_deviceMutex);
            if (!m_device) return;
        }

        if ((!m_isDanmakuEnabled.load() && danmakuItem.Mode != DanmakuMode::Subtitle)
            || (!m_isSubtitleEnabled.load() && danmakuItem.Mode == DanmakuMode::Subtitle))
            return;

        auto& layer = *m_renderLayers[layerId];

        try
        {
            // Apply opacity to text color for non-advanced/subtitle modes
            DanmakuItem itemCopy = danmakuItem;
            if (itemCopy.Mode != DanmakuMode::Advanced && itemCopy.Mode != DanmakuMode::Subtitle)
            {
                double opacity;
                { std::lock_guard<std::mutex> lock(m_settingsMutex); opacity = m_textOpacity; }
                itemCopy.TextColor.A = static_cast<uint8_t>(opacity * 255.0);
            }

            DanmakuRenderItem renderItem(itemCopy);
            if (renderItem.Mode == DanmakuMode::Unknown)
            {
                DanmakuLogger::Log(std::wstring(L"Ignore unknown danmaku: ") + renderItem.Text);
                return;
            }

            // Density control
            if (!m_autoControlDensity.load()
                && m_rollingDensity.load() > 0
                && renderItem.Mode == DanmakuMode::Rolling)
            {
                std::lock_guard<std::mutex> lock(layer.RenderListMutex);
                if (static_cast<int32_t>(layer.RenderList.size()) >= m_rollingDensity.load())
                {
                    return;
                }
            }

            winrt::Microsoft::Graphics::Canvas::CanvasDevice device;
            {
                std::lock_guard<std::mutex> lock(m_deviceMutex);
                device = m_device;
            }

            // Build text format
            auto textFormat = winrt::Microsoft::Graphics::Canvas::Text::CanvasTextFormat();
            textFormat.LocaleName(L"zh-CN");
            bool isBold = (renderItem.IsBold < 0) ? m_textBold.load() : (renderItem.IsBold > 0);
            textFormat.FontWeight(isBold
                ? winrt::Windows::UI::Text::FontWeights::Bold()
                : winrt::Windows::UI::Text::FontWeights::Normal());
            textFormat.WordWrapping(
                winrt::Microsoft::Graphics::Canvas::Text::CanvasWordWrapping::NoWrap);
            textFormat.HorizontalAlignment(
                winrt::Microsoft::Graphics::Canvas::Text::CanvasHorizontalAlignment::Center);
            textFormat.VerticalAlignment(
                winrt::Microsoft::Graphics::Canvas::Text::CanvasVerticalAlignment::Center);
            textFormat.TrimmingGranularity(
                winrt::Microsoft::Graphics::Canvas::Text::CanvasTextTrimmingGranularity::None);
            textFormat.TrimmingSign(
                winrt::Microsoft::Graphics::Canvas::Text::CanvasTrimmingSign::None);

            if (renderItem.Mode == DanmakuMode::Top
                || renderItem.Mode == DanmakuMode::Bottom
                || renderItem.Mode == DanmakuMode::Subtitle)
            {
                textFormat.WordWrapping(
                    winrt::Microsoft::Graphics::Canvas::Text::CanvasWordWrapping::Wrap);
            }

            float fontSize = renderItem.FontSize;
            if (!itemCopy.KeepDefinedFontSize)
            {
                int32_t sizeOff = (renderItem.Mode != DanmakuMode::Subtitle)
                    ? m_danmakuFontSizeOffset.load()
                    : m_subtitleFontSizeOffset.load();
                fontSize += static_cast<float>((sizeOff - 3) * (sizeOff > 3 ? 6 : 3));

                if (CanvasWidth < Standard_Canvas_Width)
                {
                    fontSize = fontSize * CanvasWidth / Standard_Canvas_Width;
                    if (fontSize >= 30.0f) fontSize *= 0.75f;
                    renderItem.MarginBottom = static_cast<int32_t>(
                        renderItem.MarginBottom * CanvasWidth * 0.75f / Standard_Canvas_Width);
                }
            }
            fontSize = static_cast<float>(static_cast<int32_t>(std::max(fontSize, 2.0f)));
            textFormat.FontSize(fontSize);

            {
                std::lock_guard<std::mutex> lock(m_settingsMutex);
                if (!renderItem.FontFamilyName.empty())
                    textFormat.FontFamily(renderItem.FontFamilyName);
                else if (renderItem.Mode == DanmakuMode::Advanced)
                    textFormat.FontFamily(Default_Font_Family_Name);
                else
                    textFormat.FontFamily(m_defaultFontFamilyName);
            }

            const winrt::hstring danmakuText(renderItem.Text);
            const int32_t maxSize = m_maxDanmakuSize.load();

            // --- Measure text ---
            {
                auto tmpRt = winrt::Microsoft::Graphics::Canvas::CanvasRenderTarget(
                    device, 0.0f, 0.0f, m_dpi);
                auto tmpDs = tmpRt.CreateDrawingSession();
                auto textLayout = winrt::Microsoft::Graphics::Canvas::Text::CanvasTextLayout(
                    tmpDs, danmakuText, textFormat, CanvasWidth - 24.0f, 0.0f);

                renderItem.Width  = static_cast<float>(textLayout.LayoutBounds().Width)  + 8.0f;
                renderItem.Height = static_cast<float>(textLayout.LayoutBounds().Height);
                if (renderItem.HasOutline)
                    renderItem.Height += renderItem.OutlineSize;

                textLayout.Close();
                tmpDs.Close();
                tmpRt.Close();

                if (renderItem.Width <= 0.0f || renderItem.Height <= 0.0f) return;
                if (maxSize > 0 && (renderItem.Width >= maxSize || renderItem.Height >= maxSize)) return;
            }

            // --- Create render target ---
            renderItem.RenderTarget = winrt::Microsoft::Graphics::Canvas::CanvasRenderTarget(
                device, renderItem.Width, renderItem.Height, m_dpi);

            {
                auto ds = renderItem.RenderTarget.CreateDrawingSession();
                auto textLayout = winrt::Microsoft::Graphics::Canvas::Text::CanvasTextLayout(
                    ds, danmakuText, textFormat, renderItem.Width, renderItem.Height);

                // Calculate initial position
                auto& ySlot = layer.YSlotManager;
                switch (renderItem.Mode)
                {
                case DanmakuMode::Rolling:
                {
                    uint32_t y = 0;
                    renderItem.NeedToReleaseYSlot = ySlot.GetY(renderItem.Id, static_cast<uint32_t>(renderItem.Height), y);
                    renderItem.StartX = CanvasWidth;
                    renderItem.StartY = static_cast<float>(y);
                    break;
                }
                case DanmakuMode::Bottom:
                {
                    uint32_t y = 0;
                    renderItem.NeedToReleaseYSlot = ySlot.GetY(renderItem.Id, static_cast<uint32_t>(renderItem.Height), y);
                    renderItem.StartY = static_cast<float>(y);
                    break;
                }
                case DanmakuMode::Top:
                {
                    uint32_t y = 0;
                    renderItem.NeedToReleaseYSlot = ySlot.GetY(renderItem.Id, static_cast<uint32_t>(renderItem.Height), y);
                    renderItem.StartY = static_cast<float>(y);
                    break;
                }
                case DanmakuMode::ReverseRolling:
                {
                    uint32_t y = 0;
                    renderItem.NeedToReleaseYSlot = ySlot.GetY(renderItem.Id, static_cast<uint32_t>(renderItem.Height), y);
                    renderItem.StartX = -renderItem.Width;
                    renderItem.StartY = static_cast<float>(y);
                    break;
                }
                case DanmakuMode::Advanced:
                {
                    if (renderItem.AlignmentMode == DanmakuAlignmentMode::Default)
                    {
                        renderItem.StartX = renderItem.DefinedStartX > 1.0f ? renderItem.DefinedStartX : renderItem.DefinedStartX * CanvasWidth;
                        renderItem.StartY = renderItem.DefinedStartY > 1.0f ? renderItem.DefinedStartY : renderItem.DefinedStartY * CanvasHeight;
                        renderItem.EndX   = renderItem.DefinedEndX   > 1.0f ? renderItem.DefinedEndX   : renderItem.DefinedEndX   * CanvasWidth;
                        renderItem.EndY   = renderItem.DefinedEndY   > 1.0f ? renderItem.DefinedEndY   : renderItem.DefinedEndY   * CanvasHeight;

                        if (renderItem.EndX > renderItem.StartX && renderItem.EndX < CanvasWidth
                            && renderItem.EndX + renderItem.Width > CanvasWidth)
                        {
                            renderItem.EndX = (renderItem.EndX + renderItem.Width * 0.2f <= CanvasWidth)
                                ? CanvasWidth - renderItem.Width : CanvasWidth;
                        }
                        if (renderItem.EndY > renderItem.StartY && renderItem.EndY < CanvasHeight
                            && renderItem.EndY + renderItem.Height > CanvasHeight)
                        {
                            renderItem.EndY = (renderItem.EndY + renderItem.Height * 0.2f <= CanvasHeight)
                                ? CanvasHeight - renderItem.Height : CanvasHeight;
                        }

                        // Apply anchor offset
                        if (renderItem.AnchorMode != DanmakuAlignmentMode::UpperLeft)
                        {
                            switch (renderItem.AnchorMode)
                            {
                            case DanmakuAlignmentMode::LowerCenter:
                            case DanmakuAlignmentMode::MiddleCenter:
                            case DanmakuAlignmentMode::UpperCenter:
                                renderItem.StartX -= renderItem.Width / 2.0f;
                                renderItem.EndX   -= renderItem.Width / 2.0f;
                                break;
                            case DanmakuAlignmentMode::LowerRight:
                            case DanmakuAlignmentMode::MiddleRight:
                            case DanmakuAlignmentMode::UpperRight:
                                renderItem.StartX -= renderItem.Width;
                                renderItem.EndX   -= renderItem.Width;
                                break;
                            default: break;
                            }
                            switch (renderItem.AnchorMode)
                            {
                            case DanmakuAlignmentMode::LowerLeft:
                            case DanmakuAlignmentMode::LowerCenter:
                            case DanmakuAlignmentMode::LowerRight:
                                renderItem.StartY -= renderItem.Height;
                                renderItem.EndY   -= renderItem.Height;
                                break;
                            case DanmakuAlignmentMode::MiddleLeft:
                            case DanmakuAlignmentMode::MiddleCenter:
                            case DanmakuAlignmentMode::MiddleRight:
                                renderItem.StartY -= renderItem.Height / 2.0f;
                                renderItem.EndY   -= renderItem.Height / 2.0f;
                                break;
                            default: break;
                            }
                        }
                    }
                    else
                    {
                        using AM = DanmakuAlignmentMode;
                        auto am = renderItem.AlignmentMode;
                        if (am == AM::LowerLeft || am == AM::MiddleLeft || am == AM::UpperLeft)
                            renderItem.StartX = static_cast<float>(renderItem.MarginLeft);
                        else if (am == AM::LowerCenter || am == AM::MiddleCenter || am == AM::UpperCenter)
                            renderItem.StartX = (CanvasWidth - renderItem.Width) / 2.0f;
                        else
                            renderItem.StartX = CanvasWidth - renderItem.Width - static_cast<float>(renderItem.MarginRight);

                        if (am == AM::LowerLeft || am == AM::LowerCenter || am == AM::LowerRight)
                            renderItem.StartY = CanvasHeight - renderItem.Height - static_cast<float>(renderItem.MarginBottom);
                        else if (am == AM::MiddleLeft || am == AM::MiddleCenter || am == AM::MiddleRight)
                            renderItem.StartY = (CanvasHeight - renderItem.Width) / 2.0f;
                        else
                            renderItem.StartY = 0.0f;

                        renderItem.EndX = renderItem.StartX;
                        renderItem.EndY = renderItem.StartY;
                    }

                    renderItem.TranslationSpeedX = renderItem.DefinedTranslationDurationMs > 0
                        ? (renderItem.EndX - renderItem.StartX) / static_cast<float>(renderItem.DefinedTranslationDurationMs) : 0.0f;
                    renderItem.TranslationSpeedY = renderItem.DefinedTranslationDurationMs > 0
                        ? (renderItem.EndY - renderItem.StartY) / static_cast<float>(renderItem.DefinedTranslationDurationMs) : 0.0f;
                    break;
                }
                case DanmakuMode::Subtitle:
                    renderItem.StartY = Subtitle_StartY;
                    break;
                default:
                    break;
                }

                renderItem.X = renderItem.StartX;

                // Density auto control
                if (m_autoControlDensity.load() && renderItem.AllowDensityControl && !renderItem.NeedToReleaseYSlot)
                {
                    textLayout.Close();
                    ds.Close();
                    return;
                }

                // Draw to render target
                auto geometry = winrt::Microsoft::Graphics::Canvas::Geometry::CanvasGeometry::CreateText(textLayout);

                ds.Clear(winrt::Windows::UI::Colors::Transparent());

                winrt::Windows::UI::Color borderColor;
                { std::lock_guard<std::mutex> lock(m_settingsMutex); borderColor = m_borderColor; }

                if (renderItem.HasBorder || DebugMode)
                {
                    ds.DrawRectangle({ 0.0f, 0.0f, renderItem.Width, renderItem.Height },
                        borderColor, 4.0f);
                }

                if (renderItem.HasOutline)
                {
                    winrt::Windows::UI::Color oc = renderItem.OutlineColor;
                    if (renderItem.TextColor.R + renderItem.TextColor.G + renderItem.TextColor.B < 0x20)
                        oc = winrt::Windows::UI::Colors::White();
                    oc.A = renderItem.TextColor.A;
                    ds.DrawGeometry(geometry, 0.0f, 0.0f, oc, renderItem.OutlineSize);
                }
                ds.FillGeometry(geometry, 0.0f, 0.0f, renderItem.TextColor);

                // Transform3D for Y-axis rotation (Advanced mode only)
                if (renderItem.Mode == DanmakuMode::Advanced
                    && (renderItem.DefinedRotateY >= 0.01f || renderItem.DefinedRotateY <= -0.01f))
                {
                    float radZ = DegreeToRadian(renderItem.DefinedRotateZ);
                    float radY = DegreeToRadian(renderItem.DefinedRotateY);

                    DirectX::XMMATRIX m1 = DirectX::XMMatrixTranslation(
                        -renderItem.Width / 2.0f, -renderItem.Height / 2.0f, 0.0f);
                    DirectX::XMMATRIX m2 = DirectX::XMMatrixIdentity();
                    if (renderItem.DefinedRotateZ >= 0.01f || renderItem.DefinedRotateZ <= -0.01f)
                        m2 = DirectX::XMMatrixRotationZ(radZ);
                    m2 = DirectX::XMMatrixMultiply(m2, DirectX::XMMatrixRotationY(radY));

                    // Apply perspective transform: M14 = -(1/width) * sin(radY)
                    DirectX::XMFLOAT4X4 m2f;
                    DirectX::XMStoreFloat4x4(&m2f, m2);
                    m2f._14 = -(1.0f / renderItem.Width) * std::sin(radY);
                    m2 = DirectX::XMLoadFloat4x4(&m2f);

                    DirectX::XMMATRIX m3 = DirectX::XMMatrixTranslation(
                        renderItem.Width / 2.0f, renderItem.Height / 2.0f, 0.0f);

                    DirectX::XMMATRIX combined = DirectX::XMMatrixMultiply(
                        DirectX::XMMatrixMultiply(m1, m2), m3);

                    DirectX::XMFLOAT4X4 result;
                    DirectX::XMStoreFloat4x4(&result, combined);

                    winrt::Windows::Foundation::Numerics::float4x4 transform;
                    static_assert(sizeof(transform) == sizeof(result));
                    std::memcpy(&transform, &result, sizeof(result));

                    renderItem.TransformEffect = winrt::Microsoft::Graphics::Canvas::Effects::Transform3DEffect();
                    renderItem.TransformEffect.TransformMatrix(transform);
                    renderItem.TransformEffect.Source(renderItem.RenderTarget);
                    renderItem.SourceRect = renderItem.TransformEffect.GetBounds(renderItem.RenderTarget);
                }

                geometry.Close();
                textLayout.Close();
                ds.Close();
            }

            // Add to render list
            {
                std::lock_guard<std::mutex> lock(layer.RenderListMutex);
                layer.RenderList.push_back(std::move(renderItem));
            }
        }
        catch (winrt::hresult_error const& ex)
        {
            DanmakuLogger::Log(std::wstring(L"RenderDanmakuItem error: ") + ex.message().c_str());

            std::lock_guard<std::mutex> dlock(m_deviceMutex);
            if (m_device && m_device.IsDeviceLost(ex.code()))
            {
                try { m_device.RaiseDeviceLost(); } catch (...) {}
                for (auto& layer2 : m_renderLayers) layer2->Clear();
            }
        }
        catch (std::exception const& ex)
        {
            DanmakuLogger::Log(ex.what());
        }
    }

    // =========================================================================
    // State control
    // =========================================================================

    void DanmakuRender::SetRenderState(bool renderDanmaku, bool renderSubtitle)
    {
        m_isDanmakuEnabled  = renderDanmaku;
        m_isSubtitleEnabled = renderSubtitle;

        if (!renderDanmaku)
        {
            for (auto& layer : m_renderLayers)
                if (!layer->IsSubtitleLayer) layer->Clear();
        }
        if (!renderSubtitle)
        {
            for (auto& layer : m_renderLayers)
                if (layer->IsSubtitleLayer) layer->Clear();
        }

        if (m_canvas)
        {
            bool startRender = renderDanmaku || renderSubtitle;
            if (!startRender)
            {
                Pause();
                Stop();
            }
            else if (startRender && (!renderDanmaku || !renderSubtitle) && m_canvas.Paused())
            {
                Start();
            }
        }
    }

    void DanmakuRender::SetLayerRenderState(uint32_t layerId, bool render)
    {
        if (layerId < LayerCount)
            m_renderLayers[layerId]->IsEnabled = render;
    }

    void DanmakuRender::SetSubtitleLayer(uint32_t layerId)
    {
        if (layerId < LayerCount)
            m_renderLayers[layerId]->SetSubtitleLayer(true);
    }

    void DanmakuRender::ClearLayer(uint32_t layerId)
    {
        if (layerId < LayerCount)
            m_renderLayers[layerId]->Clear();
    }

    void DanmakuRender::Clear()
    {
        for (auto& layer : m_renderLayers) layer->Clear();
        DanmakuLogger::Log(L"DanmakuRender is cleared");
    }

    void DanmakuRender::Start()
    {
        if (!m_isDanmakuEnabled.load() && !m_isSubtitleEnabled.load()) return;

        auto canvas = m_canvas;
        if (canvas)
        {
            canvas.Paused(false);
            auto dispatcher = canvas.Dispatcher();
            if (dispatcher)
            {
                // Show canvas after a short delay (matches C# Task.Delay(50))
                winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
                    [canvas, isStopped = &m_isStopped](
                        winrt::Windows::System::Threading::ThreadPoolTimer const&)
                    {
                        if (isStopped->load()) return;
                        canvas.Dispatcher().RunAsync(
                            winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                            [canvas, isStopped]()
                            {
                                if (!isStopped->load() && canvas)
                                    canvas.Visibility(winrt::Windows::UI::Xaml::Visibility::Visible);
                            });
                    },
                    std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
                        std::chrono::milliseconds(50)));
            }
        }
        m_isStopped = false;
        DanmakuLogger::Log(L"DanmakuRender is started");
    }

    void DanmakuRender::Pause()
    {
        if (m_canvas) m_canvas.Paused(true);
        DanmakuLogger::Log(L"DanmakuRender is paused");
    }

    void DanmakuRender::Stop()
    {
        m_isStopped = true;
        Clear();
        auto canvas = m_canvas;
        if (canvas)
        {
            auto dispatcher = canvas.Dispatcher();
            if (dispatcher)
            {
                dispatcher.RunAsync(
                    winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                    [canvas]()
                    {
                        if (canvas)
                            canvas.Visibility(winrt::Windows::UI::Xaml::Visibility::Collapsed);
                    });
            }
        }
        DanmakuLogger::Log(L"DanmakuRender is stopped");
    }

    void DanmakuRender::Close() noexcept
    {
        m_isStopped = true;
        try
        {
            for (auto& layer : m_renderLayers)
                if (layer) layer->Clear();

            if (m_canvas)
            {
                m_canvas.Paused(true);
                m_canvas.SizeChanged(m_sizeChangedToken);
                m_canvas.CreateResources(m_createResourcesToken);
                m_canvas.Update(m_updateToken);
                m_canvas.Draw(m_drawToken);
                m_canvas = nullptr;
            }
        }
        catch (...) {}
        DanmakuLogger::Log(L"DanmakuRender is closed");
    }

    // =========================================================================
    // Event handlers
    // =========================================================================

    void DanmakuRender::OnSizeChanged(
        winrt::Windows::Foundation::IInspectable const&,
        winrt::Windows::UI::Xaml::SizeChangedEventArgs const& args)
    {
        auto sz = args.NewSize();
        CanvasWidth  = static_cast<float>(sz.Width);
        CanvasHeight = static_cast<float>(sz.Height);
        for (auto& layer : m_renderLayers)
            layer->UpdateYSlotManagerLength(
                static_cast<uint32_t>(sz.Height), m_rollingAreaRatio.load());
        DanmakuLogger::Log(std::wstring(L"Update canvas size: ")
            + std::to_wstring(static_cast<int>(CanvasWidth))
            + L"x" + std::to_wstring(static_cast<int>(CanvasHeight)));
    }

    void DanmakuRender::OnCreateResources(
        winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasAnimatedControl const& sender,
        winrt::Microsoft::Graphics::Canvas::UI::CanvasCreateResourcesEventArgs const& args)
    {
        DanmakuLogger::Log(L"CreateResources");
        if (args.Reason() == winrt::Microsoft::Graphics::Canvas::UI::CanvasCreateResourcesReason::NewDevice)
        {
            std::lock_guard<std::mutex> lock(m_deviceMutex);
            if (m_device)
            {
                try { m_device.RaiseDeviceLost(); }
                catch (winrt::hresult_error const& ex)
                {
                    DanmakuLogger::Log(std::wstring(L"RaiseDeviceLost failed: ") + ex.message().c_str());
                }
            }
        }

        std::lock_guard<std::mutex> lock(m_deviceMutex);
        m_device = sender.Device();
        float dpi = sender.Dpi();
        m_maxDanmakuSize = (dpi > 0)
            ? static_cast<int32_t>(
                m_device.MaximumBitmapSizeInPixels() / (dpi / 96.0f))
            : 0;
    }

    // =========================================================================
    // Update loop
    // =========================================================================

    void DanmakuRender::OnUpdate(
        winrt::Microsoft::Graphics::Canvas::UI::Xaml::ICanvasAnimatedControl const& sender,
        winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasAnimatedUpdateEventArgs const& args)
    {
        const float rollingSpeed = m_rollingSpeed.load();
        const bool  paused       = sender.Paused();

        for (uint32_t layerId = 0; layerId < LayerCount; ++layerId)
        {
            auto& layer    = *m_renderLayers[layerId];
            auto& ySlot    = layer.YSlotManager;

            std::lock_guard<std::mutex> lock(layer.RenderListMutex);
            auto& renderList = layer.RenderList;

            for (int i = static_cast<int>(renderList.size()) - 1; i >= 0; --i)
            {
                if (m_isStopped.load()) return;

                auto& ri = renderList[static_cast<size_t>(i)];
                if (!ri.IsFirstRenderTimeSet)
                {
                    ri.FirstRenderTime    = args.Timing().TotalTime;
                    ri.IsFirstRenderTimeSet = true;
                }

                float elapsedMs  = TimeSpanToMs(args.Timing().ElapsedTime);
                float durationMs = TimeSpanToMs(
                    winrt::Windows::Foundation::TimeSpan{
                        args.Timing().TotalTime.count() - ri.FirstRenderTime.count() });
                bool removeItem = false;

                switch (ri.Mode)
                {
                case DanmakuMode::Rolling:
                {
                    if (!paused)
                        ri.X -= elapsedMs * AdjustRollingSpeedByWidth(rollingSpeed, ri.Width);
                    if (ri.NeedToReleaseYSlot && ri.X < CanvasWidth - ri.Width - 48.0f)
                    {
                        ySlot.ReleaseYSlot(ri.Id, static_cast<uint32_t>(ri.StartY));
                        ri.NeedToReleaseYSlot = false;
                    }
                    if (ri.X < -ri.Width) removeItem = true;
                    break;
                }
                case DanmakuMode::Bottom:
                case DanmakuMode::Top:
                {
                    ri.X = (CanvasWidth - ri.Width) / 2.0f;
                    float maxDur = (ri.DefinedDurationMs > 0)
                        ? static_cast<float>(ri.DefinedDurationMs) : Default_BottomAndTop_Duration_Ms;
                    if (durationMs > maxDur)
                    {
                        removeItem = true;
                        if (ri.NeedToReleaseYSlot)
                            ySlot.ReleaseYSlot(ri.Id, static_cast<uint32_t>(ri.StartY));
                    }
                    break;
                }
                case DanmakuMode::ReverseRolling:
                {
                    if (!paused)
                        ri.X += elapsedMs * AdjustRollingSpeedByWidth(rollingSpeed, ri.Width);
                    if (ri.NeedToReleaseYSlot && ri.X > 48.0f)
                    {
                        ySlot.ReleaseYSlot(ri.Id, static_cast<uint32_t>(ri.StartY));
                        ri.NeedToReleaseYSlot = false;
                    }
                    if (ri.X >= CanvasWidth) removeItem = true;
                    break;
                }
                case DanmakuMode::Advanced:
                {
                    if (durationMs <= static_cast<float>(ri.DefinedDurationMs))
                    {
                        if (durationMs >= static_cast<float>(ri.DefinedTranslationDelayMs))
                        {
                            float transDur = static_cast<float>(ri.DefinedTranslationDurationMs);
                            float elapsed2 = durationMs - static_cast<float>(ri.DefinedTranslationDelayMs);
                            if (elapsed2 < transDur)
                            {
                                ri.X = ri.StartX + ri.TranslationSpeedX * elapsed2;
                                ri.Y = ri.StartY + ri.TranslationSpeedY * elapsed2;
                            }
                            else
                            {
                                ri.X = ri.EndX;
                                ri.Y = ri.EndY;
                            }
                        }
                        if (durationMs >= static_cast<float>(ri.DefinedAlphaDelayMs)
                            && ri.DefinedEndAlpha != ri.DefinedStartAlpha)
                        {
                            float alphaDur = static_cast<float>(ri.DefinedAlphaDurationMs);
                            float elapsed3 = durationMs - static_cast<float>(ri.DefinedAlphaDelayMs);
                            if (elapsed3 < alphaDur)
                            {
                                ri.Alpha = static_cast<uint8_t>(ri.DefinedStartAlpha
                                    + (ri.DefinedEndAlpha - ri.DefinedStartAlpha) * elapsed3 / alphaDur);
                            }
                            else
                            {
                                ri.Alpha = ri.DefinedEndAlpha;
                            }
                        }
                    }
                    else { removeItem = true; }
                    break;
                }
                case DanmakuMode::Subtitle:
                {
                    if ((static_cast<int>(renderList.size()) > 1 && i < static_cast<int>(renderList.size()) - 1)
                        || durationMs > static_cast<float>(ri.DefinedDurationMs))
                    {
                        removeItem = true;
                    }
                    else
                    {
                        ri.X = (CanvasWidth - ri.Width) / 2.0f;
                    }
                    break;
                }
                default: break;
                }

                if (removeItem)
                {
                    ri.Close();
                    renderList.erase(renderList.begin() + i);
                }
            }
        }
    }

    // =========================================================================
    // Draw
    // =========================================================================

    void DanmakuRender::OnDraw(
        winrt::Microsoft::Graphics::Canvas::UI::Xaml::ICanvasAnimatedControl const&,
        winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasAnimatedDrawEventArgs const& args)
    {
        auto drawSession = args.DrawingSession();
        try
        {
            int32_t totalCount = 0;

            for (uint32_t layerId = 0; layerId < LayerCount; ++layerId)
            {
                auto& layer = *m_renderLayers[layerId];
                if (!layer.IsEnabled) continue;

                std::lock_guard<std::mutex> lock(layer.RenderListMutex);
                if (layer.RenderList.empty()) continue;

                auto sortMode = layer.RequireStrictRenderOrder
                    ? winrt::Microsoft::Graphics::Canvas::CanvasSpriteSortMode::None
                    : winrt::Microsoft::Graphics::Canvas::CanvasSpriteSortMode::Bitmap;

                auto spriteBatch = drawSession.CreateSpriteBatch(sortMode);

                for (auto& ri : layer.RenderList)
                {
                    if (m_isStopped.load())
                    {
                        spriteBatch.Close();
                        return;
                    }
                    if (!ri.IsFirstRenderTimeSet) continue;

                    ++totalCount;

                    switch (ri.Mode)
                    {
                    case DanmakuMode::Rolling:
                    case DanmakuMode::Top:
                    {
                        spriteBatch.Draw(ri.RenderTarget,
                            winrt::Windows::Foundation::Numerics::float2{ ri.X, ri.StartY });
                        break;
                    }
                    case DanmakuMode::ReverseRolling:
                    {
                        spriteBatch.Draw(ri.RenderTarget,
                            winrt::Windows::Foundation::Numerics::float2{ ri.X, ri.StartY });
                        break;
                    }
                    case DanmakuMode::Bottom:
                    {
                        float noOvl   = m_noOverlapSubtitle.load()
                            ? std::max(CanvasHeight - 100.0f, CanvasHeight * 0.8f) : CanvasHeight;
                        float y = std::max(noOvl - ri.Height - ri.StartY, 0.0f)
                            - static_cast<float>(ri.MarginBottom);
                        spriteBatch.Draw(ri.RenderTarget,
                            winrt::Windows::Foundation::Numerics::float2{ ri.X, y });
                        break;
                    }
                    case DanmakuMode::Advanced:
                    {
                        float opacity = static_cast<float>(ri.Alpha) / 255.0f;
                        if (ri.TransformEffect)
                        {
                            winrt::Windows::Foundation::Rect targetRect = ri.SourceRect;
                            targetRect.X += ri.X;
                            targetRect.Y += ri.Y;
                            // Draw directly (Transform3DEffect can't use CanvasSpriteBatch)
                            drawSession.DrawImage(ri.TransformEffect, targetRect, ri.SourceRect, opacity);
                        }
                        else
                        {
                            winrt::Windows::Foundation::Numerics::float4 tint{ opacity, opacity, opacity, opacity };
                            // Tint in sprite batch: RGBA, so use { 1,1,1,opacity } ... but
                            // Win2D sprite batch tint mixes with color, so: { 1,1,1,alpha }
                            tint = { 1.0f, 1.0f, 1.0f, opacity };

                            if (ri.DefinedRotateZ >= 0.01f || ri.DefinedRotateZ <= -0.01f)
                            {
                                float radZ = DegreeToRadian(ri.DefinedRotateZ);
                                winrt::Windows::Foundation::Numerics::float2 pos{
                                    ri.X + ri.Width / 2.0f, ri.Y + ri.Height / 2.0f };
                                winrt::Windows::Foundation::Numerics::float2 origin{
                                    ri.Width / 2.0f, ri.Height / 2.0f };
                                spriteBatch.Draw(ri.RenderTarget, pos, tint, origin, radZ,
                                    winrt::Windows::Foundation::Numerics::float2{ 1.0f, 1.0f },
                                    winrt::Microsoft::Graphics::Canvas::CanvasSpriteFlip::None);
                            }
                            else
                            {
                                spriteBatch.Draw(ri.RenderTarget,
                                    winrt::Windows::Foundation::Numerics::float2{ ri.X, ri.Y },
                                    tint);
                            }
                        }
                        break;
                    }
                    case DanmakuMode::Subtitle:
                    {
                        float y = CanvasHeight - ri.Height - ri.StartY;
                        {
                            auto layer2 = drawSession.CreateLayer(0.7f);
                            drawSession.FillRectangle(
                                { ri.X - 4.0f, y - 4.0f, ri.Width + 8.0f, ri.Height + 8.0f },
                                winrt::Windows::UI::Colors::Black());
                            layer2.Close();
                        }
                        spriteBatch.Draw(ri.RenderTarget,
                            winrt::Windows::Foundation::Numerics::float2{ ri.X, y });
                        break;
                    }
                    default: break;
                    }
                }
                spriteBatch.Close();
            }

            if (DebugMode)
            {
                auto timing = args.Timing();
                if (timing.ElapsedTime.count() > 0)
                {
                    int fps = static_cast<int>(
                        10000000.0 / static_cast<double>(timing.ElapsedTime.count()));
                    uint64_t memMb = winrt::Windows::System::MemoryManager::AppMemoryUsage()
                        / (1024u * 1024u);
                    drawSession.FillRectangle({ 0, 0, 410, 30 },
                        fps >= 30 ? winrt::Windows::UI::Colors::Gray()
                                  : winrt::Windows::UI::Colors::Red());
                    std::wstring dbgTxt = std::wstring(L"fps:") + std::to_wstring(fps)
                        + L" count:" + std::to_wstring(totalCount)
                        + L" " + std::to_wstring(static_cast<int>(CanvasWidth))
                        + L"x" + std::to_wstring(static_cast<int>(CanvasHeight))
                        + L" " + std::to_wstring(memMb) + L"MB";
                    drawSession.DrawText(dbgTxt, 0.0f, 0.0f,
                        winrt::Windows::UI::Colors::LightGreen());
                }
            }
        }
        catch (winrt::hresult_error const& ex)
        {
            DanmakuLogger::Log(std::wstring(L"OnDraw error: ") + ex.message().c_str());
            std::lock_guard<std::mutex> lock(m_deviceMutex);
            if (m_device && m_device.IsDeviceLost(ex.code()))
                DanmakuLogger::Log(L"Device is lost!");
        }
        catch (...) {}

        try { if (drawSession) drawSession.Close(); } catch (...) {}
    }

    // =========================================================================
    // Helpers
    // =========================================================================

    float DanmakuRender::DegreeToRadian(float degree) noexcept
    {
        return degree * 3.14159265358979323846f / 180.0f;
    }

    float DanmakuRender::AdjustRollingSpeedByWidth(float speed, float width) noexcept
    {
        return speed * (std::min(width * 0.0015f, 0.2f) + 1.0f);
    }

    float DanmakuRender::TimeSpanToMs(winrt::Windows::Foundation::TimeSpan ts) noexcept
    {
        // TimeSpan.count() is in 100-nanosecond ticks
        return static_cast<float>(ts.count() / 10000.0);
    }

} // namespace DanmakuFrostMasterXx
