#include "pch.h"
#include "DanmakuFrostMaster.h"
#include "DanmakuFrostMaster.g.cpp"

namespace winrt::DanmakuFrostMasterXx::implementation
{
    // -----------------------------------------------------------------------
    // Destructor
    // -----------------------------------------------------------------------
    DanmakuFrostMaster::~DanmakuFrostMaster()
    {
        Close();
    }

    // -----------------------------------------------------------------------
    // Initialize — must be called on the UI thread
    // -----------------------------------------------------------------------
    void DanmakuFrostMaster::Initialize(winrt::Windows::Foundation::IInspectable const& canvas)
    {
        auto animatedCanvas =
            canvas.as<winrt::Microsoft::Graphics::Canvas::UI::Xaml::CanvasAnimatedControl>();

        m_render = std::make_unique<::DanmakuFrostMasterXx::DanmakuRender>(animatedCanvas);
        m_isRenderEnabled = true;

        // Start background updater (fire-and-forget, mirrors C# ThreadPool.RunAsync)
        auto self = this;
        winrt::Windows::System::Threading::ThreadPool::RunAsync(
            [self](winrt::Windows::Foundation::IAsyncAction const&)
            {
                self->Updater_DoWork();
            },
            winrt::Windows::System::Threading::WorkItemPriority::High);

        ::DanmakuFrostMasterXx::DanmakuLogger::Log(L"DanmakuFrostMaster is created");
    }

    // -----------------------------------------------------------------------
    // DebugMode property
    // -----------------------------------------------------------------------
    bool DanmakuFrostMaster::DebugMode()
    {
        return m_render ? m_render->DebugMode : false;
    }

    void DanmakuFrostMaster::DebugMode(bool value)
    {
        if (m_render) m_render->DebugMode = value;
    }

    // -----------------------------------------------------------------------
    // Render settings — delegates to DanmakuRender
    // -----------------------------------------------------------------------
    void DanmakuFrostMaster::SetAutoControlDensity(bool value)
    {
        if (m_render) m_render->SetAutoControlDensity(value);
    }

    void DanmakuFrostMaster::SetRollingDensity(int32_t value)
    {
        if (m_render) m_render->SetRollingDensity(value);
    }

    void DanmakuFrostMaster::SetRollingAreaRatio(int32_t value)
    {
        if (m_render) m_render->SetRollingAreaRatio(value);
    }

    void DanmakuFrostMaster::SetRollingSpeed(int32_t value)
    {
        if (m_render) m_render->SetRollingSpeed(value);
    }

    void DanmakuFrostMaster::SetOpacity(double value)
    {
        if (m_render) m_render->SetOpacity(value);
    }

    void DanmakuFrostMaster::SetIsTextBold(bool value)
    {
        if (m_render) m_render->SetIsTextBold(value);
    }

    void DanmakuFrostMaster::SetDanmakuFontSizeOffset(winrt::DanmakuFrostMasterXx::DanmakuFontSize value)
    {
        if (m_render) m_render->SetDanmakuFontSizeOffset(static_cast<int32_t>(value));
    }

    void DanmakuFrostMaster::SetSubtitleFontSize(winrt::DanmakuFrostMasterXx::DanmakuFontSize value)
    {
        if (m_render) m_render->SetSubtitleFontSizeOffset(static_cast<int32_t>(value));
    }

    void DanmakuFrostMaster::SetFontSizeOffset(int32_t value)
    {
        // Consumer convenience: clamp to valid DanmakuFontSize range [1,5] and set both
        int32_t clamped = std::clamp(value, 1, 5);
        if (m_render)
        {
            m_render->SetDanmakuFontSizeOffset(clamped);
            m_render->SetSubtitleFontSizeOffset(clamped);
        }
    }

    void DanmakuFrostMaster::SetFontFamilyName(winrt::hstring const& value)
    {
        if (m_render) m_render->SetDefaultFontFamilyName(std::wstring(value.c_str()));
    }

    void DanmakuFrostMaster::SetBorderColor(winrt::Windows::UI::Color borderColor)
    {
        if (m_render) m_render->SetBorderColor(borderColor);
    }

    void DanmakuFrostMaster::SetNoOverlapSubtitle(bool value)
    {
        if (m_render) m_render->SetNoOverlapSubtitle(value);
    }

    // -----------------------------------------------------------------------
    // Playback control
    // -----------------------------------------------------------------------
    void DanmakuFrostMaster::UpdateTime(uint32_t currentMs)
    {
        {
            std::lock_guard<std::mutex> lk(m_updateTimeMutex);
            m_updateTimeQueue.push(currentMs);
        }
        SignalUpdate();
    }

    void DanmakuFrostMaster::Pause()
    {
        if (!m_isClosing)
        {
            SetPauseSignal(false);
            if (m_render) m_render->Pause();
        }
    }

    void DanmakuFrostMaster::Resume()
    {
        if (m_render) m_render->Start();
        SetPauseSignal(true);
    }

    void DanmakuFrostMaster::Stop()
    {
        Pause();
        if (m_render) m_render->Stop();
        {
            std::lock_guard<std::mutex> lk(m_updateTimeMutex);
            while (!m_updateTimeQueue.empty()) m_updateTimeQueue.pop();
        }
    }

    void DanmakuFrostMaster::Restart()
    {
        Seek(0);
    }

    void DanmakuFrostMaster::Seek(uint32_t targetMs)
    {
        m_isSeeking = true;
        Stop();

        std::lock_guard<std::recursive_mutex> lk(m_danmakuListMutex);

        int32_t lastIdx = 0;
        const int32_t listSize = static_cast<int32_t>(m_danmakuList.size());

        if (listSize > 0)
        {
            while (lastIdx < listSize && m_danmakuList[lastIdx].StartMs < targetMs)
            {
                lastIdx++;
            }

            if (m_hasSubtitle)
            {
                if (m_render)
                {
                    m_render->ClearLayer(
                        ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::SubtitleLayerId);
                }

                int32_t idx = lastIdx - 1;
                while (idx >= 0 &&
                       m_danmakuList[idx].Mode != ::DanmakuFrostMasterXx::DanmakuMode::Subtitle)
                {
                    idx--;
                }

                if (idx >= 0 && idx != lastIdx &&
                    (uint64_t)m_danmakuList[idx].StartMs + m_danmakuList[idx].DurationMs > targetMs)
                {
                    m_subtitleIndexAfterSeek.store(idx);
                }
            }
        }

        m_lastIndex.store(lastIdx);
        m_lastTimeMs.store(targetMs);
        Resume();
        m_isSeeking = false;
    }

    // -----------------------------------------------------------------------
    // Render layer state
    // -----------------------------------------------------------------------
    void DanmakuFrostMaster::SetRenderState(bool renderDanmaku, bool renderSubtitle)
    {
        m_isRenderEnabled = renderDanmaku || renderSubtitle;
        if (m_render) m_render->SetRenderState(renderDanmaku, renderSubtitle);
    }

    void DanmakuFrostMaster::SetLayerRenderState(uint32_t layerId, bool render)
    {
        if (m_render) m_render->SetLayerRenderState(layerId, render);
    }

    void DanmakuFrostMaster::SetSubtitleLayer(uint32_t layerId)
    {
        if (m_render) m_render->SetSubtitleLayer(layerId);
    }

    // -----------------------------------------------------------------------
    // Danmaku list management
    // -----------------------------------------------------------------------
    void DanmakuFrostMaster::SetDanmakuListFromXml(winrt::hstring const& xmlStr)
    {
        auto result = ::DanmakuFrostMasterXx::BilibiliXmlParser::ParseXml(
            std::wstring(xmlStr.c_str()), {}, true);

        std::lock_guard<std::recursive_mutex> lk(m_danmakuListMutex);
        m_hasSubtitle = false;
        m_danmakuList = std::move(result.Items);
    }

    void DanmakuFrostMaster::SetSubtitleListFromJson(winrt::hstring const& jsonStr)
    {
        auto subtitleList = ::DanmakuFrostMasterXx::BilibiliXmlParser::ParseSubtitleJson(
            std::wstring(jsonStr.c_str()));

        if (m_render)
        {
            m_render->ClearLayer(
                ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::SubtitleLayerId);
        }

        std::lock_guard<std::recursive_mutex> lk(m_danmakuListMutex);

        // Remove existing subtitle items
        m_danmakuList.erase(
            std::remove_if(m_danmakuList.begin(), m_danmakuList.end(),
                [](const ::DanmakuFrostMasterXx::DanmakuItem& item)
                {
                    return item.Mode == ::DanmakuFrostMasterXx::DanmakuMode::Subtitle;
                }),
            m_danmakuList.end());

        if (!subtitleList.empty())
        {
            m_hasSubtitle = true;
            const uint32_t lastMs = m_lastTimeMs.load();

            // Merge-insert subtitles into danmaku list (same logic as C# SetSubtitleList)
            size_t index1 = 0, index2 = 0;
            while (index1 < m_danmakuList.size() && index2 < subtitleList.size())
            {
                if (m_danmakuList[index1].StartMs > subtitleList[index2].StartMs)
                {
                    const auto& sub = subtitleList[index2];
                    if (lastMs > 0 && sub.StartMs < lastMs &&
                        (uint64_t)sub.StartMs + sub.DurationMs > lastMs)
                    {
                        if (m_render)
                        {
                            m_render->RenderDanmakuItem(
                                ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::SubtitleLayerId,
                                sub);
                        }
                    }
                    m_danmakuList.insert(m_danmakuList.begin() + index1, sub);
                    index2++;
                }
                index1++;
            }

            // Append any remaining subtitles
            if (index1 == m_danmakuList.size())
            {
                for (; index2 < subtitleList.size(); index2++)
                {
                    const auto& sub = subtitleList[index2];
                    if (lastMs > 0 && sub.StartMs < lastMs &&
                        (uint64_t)sub.StartMs + sub.DurationMs > lastMs)
                    {
                        if (m_render)
                        {
                            m_render->RenderDanmakuItem(
                                ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::SubtitleLayerId,
                                sub);
                        }
                    }
                    m_danmakuList.push_back(sub);
                }
            }

            const int32_t curLastIndex = m_lastIndex.load();
            const int32_t newSize = static_cast<int32_t>(m_danmakuList.size());
            if (curLastIndex >= newSize)
            {
                m_lastIndex.store(std::max(0, newSize - 1));
            }
        }
    }

    void DanmakuFrostMaster::Clear()
    {
        std::lock_guard<std::recursive_mutex> lk(m_danmakuListMutex);
        m_danmakuList.clear();
    }

    void DanmakuFrostMaster::ClearLayer(uint32_t layerId)
    {
        if (m_render) m_render->ClearLayer(layerId);
    }

    // -----------------------------------------------------------------------
    // Close
    // -----------------------------------------------------------------------
    void DanmakuFrostMaster::Close()
    {
        bool expected = false;
        if (m_isClosing.compare_exchange_strong(expected, true))
        {
            m_isRenderEnabled = false;

            // Wake pause-waiting thread
            SetPauseSignal(true);
            // Wake update-waiting thread
            SignalUpdate();

            if (m_render) m_render->Close();

            ::DanmakuFrostMasterXx::DanmakuLogger::Log(L"DanmakuFrostMaster is closed");
        }
    }

    // -----------------------------------------------------------------------
    // AutoResetEvent helpers
    // -----------------------------------------------------------------------
    void DanmakuFrostMaster::WaitUpdateEvent()
    {
        std::unique_lock<std::mutex> lk(m_updateMutex);
        m_updateCv.wait(lk, [this] { return m_updateSignaled || m_isClosing.load(); });
        m_updateSignaled = false; // auto-reset
    }

    void DanmakuFrostMaster::SignalUpdate()
    {
        {
            std::lock_guard<std::mutex> lk(m_updateMutex);
            m_updateSignaled = true;
        }
        m_updateCv.notify_one();
    }

    // -----------------------------------------------------------------------
    // ManualResetEventSlim helpers
    // -----------------------------------------------------------------------
    void DanmakuFrostMaster::WaitPauseEvent()
    {
        std::unique_lock<std::mutex> lk(m_pauseMutex);
        m_pauseCv.wait(lk, [this] { return m_pauseSignaled || m_isClosing.load(); });
    }

    void DanmakuFrostMaster::SetPauseSignal(bool signaled)
    {
        {
            std::lock_guard<std::mutex> lk(m_pauseMutex);
            m_pauseSignaled = signaled;
        }
        if (signaled)
        {
            m_pauseCv.notify_all();
        }
    }

    // -----------------------------------------------------------------------
    // Background updater thread
    // -----------------------------------------------------------------------
    void DanmakuFrostMaster::Updater_DoWork()
    {
        try
        {
            while (!m_isClosing)
            {
                // Wait for UpdateTime() to signal (AutoResetEvent)
                WaitUpdateEvent();
                if (m_isClosing) break;

                // Wait while paused (ManualResetEventSlim)
                WaitPauseEvent();
                if (m_isClosing) break;

                // Dequeue next timestamp
                uint32_t currentTimeMs = 0;
                {
                    std::lock_guard<std::mutex> lk(m_updateTimeMutex);
                    if (!m_updateTimeQueue.empty())
                    {
                        currentTimeMs = m_updateTimeQueue.front();
                        m_updateTimeQueue.pop();
                    }
                }
                if (currentTimeMs == 0) continue;

                std::lock_guard<std::recursive_mutex> lk(m_danmakuListMutex);

                // Time rewind or long suspension → re-seek
                const uint32_t lastMs = m_lastTimeMs.load();
                if (currentTimeMs < lastMs || currentTimeMs - lastMs > 5000)
                {
                    ::DanmakuFrostMasterXx::DanmakuLogger::Log(
                        L"Reseek after a long time suspension");
                    Seek(currentTimeMs);
                }
                else
                {
                    m_lastTimeMs.store(currentTimeMs);
                }

                bool subtitleRendered = false;
                int32_t lastIdx  = m_lastIndex.load();
                const int32_t listSize = static_cast<int32_t>(m_danmakuList.size());

                while (currentTimeMs > 0 &&
                       lastIdx < listSize &&
                       m_danmakuList[lastIdx].StartMs <= currentTimeMs)
                {
                    if (m_isClosing) return;
                    if (m_isSeeking)  break;

                    // Skip realtime items (already rendered on add)
                    bool skip = m_danmakuList[lastIdx].IsRealtime;
                    if (skip) m_danmakuList[lastIdx].IsRealtime = false;

                    if (!skip && m_isRenderEnabled && m_render)
                    {
                        uint32_t layerId;
                        switch (m_danmakuList[lastIdx].Mode)
                        {
                        case ::DanmakuFrostMasterXx::DanmakuMode::Bottom:
                            layerId = ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::BottomLayerId;
                            break;
                        case ::DanmakuFrostMasterXx::DanmakuMode::Top:
                            layerId = ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::TopLayerId;
                            break;
                        case ::DanmakuFrostMasterXx::DanmakuMode::ReverseRolling:
                            layerId = ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::ReverseRollingLayerId;
                            break;
                        case ::DanmakuFrostMasterXx::DanmakuMode::Advanced:
                            layerId = ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::AdvancedLayerId;
                            break;
                        case ::DanmakuFrostMasterXx::DanmakuMode::Subtitle:
                            subtitleRendered = true;
                            layerId = ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::SubtitleLayerId;
                            break;
                        default:
                            layerId = ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::RollingLayerId;
                            break;
                        }
                        m_render->RenderDanmakuItem(layerId, m_danmakuList[lastIdx]);
                    }

                    lastIdx++;
                }
                m_lastIndex.store(lastIdx);

                // Render subtitle that was visible just before a seek point
                const int32_t subtitleIdx = m_subtitleIndexAfterSeek.load();
                if (subtitleIdx >= 0 && subtitleIdx < listSize)
                {
                    if (!subtitleRendered && m_render)
                    {
                        m_render->RenderDanmakuItem(
                            ::DanmakuFrostMasterXx::DanmakuDefaultLayerDef::SubtitleLayerId,
                            m_danmakuList[subtitleIdx]);
                    }
                    m_subtitleIndexAfterSeek.store(-1);
                }
            }
        }
        catch (...)
        {
            ::DanmakuFrostMasterXx::DanmakuLogger::Log(L"Updater_DoWork: unhandled exception");
        }
        ::DanmakuFrostMasterXx::DanmakuLogger::Log(L"Updater_DoWork: exited");
    }
}

