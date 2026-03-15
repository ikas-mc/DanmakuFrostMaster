#pragma once

#include "DanmakuFrostMaster.g.h"
#include "DanmakuRender.h"
#include "DanmakuTypes.h"
#include "DanmakuLogger.h"
#include "BilibiliXmlParser.h"

namespace winrt::DanmakuFrostMasterXx::implementation
{
    struct DanmakuFrostMaster : DanmakuFrostMasterT<DanmakuFrostMaster>
    {
        DanmakuFrostMaster() = default;
        ~DanmakuFrostMaster();

        void Initialize(winrt::Windows::Foundation::IInspectable const& canvas);

        bool DebugMode();
        void DebugMode(bool value);

        void SetAutoControlDensity(bool value);
        void SetRollingDensity(int32_t value);
        void SetRollingAreaRatio(int32_t value);
        void SetRollingSpeed(int32_t value);
        void SetOpacity(double value);
        void SetIsTextBold(bool value);
        void SetDanmakuFontSizeOffset(winrt::DanmakuFrostMasterXx::DanmakuFontSize value);
        void SetSubtitleFontSize(winrt::DanmakuFrostMasterXx::DanmakuFontSize value);
        void SetFontSizeOffset(int32_t value);
        void SetFontFamilyName(winrt::hstring const& value);
        void SetBorderColor(winrt::Windows::UI::Color borderColor);
        void SetNoOverlapSubtitle(bool value);

        void UpdateTime(uint32_t currentMs);
        void Pause();
        void Resume();
        void Stop();
        void Restart();
        void Seek(uint32_t targetMs);

        void SetRenderState(bool renderDanmaku, bool renderSubtitle);
        void SetLayerRenderState(uint32_t layerId, bool render);
        void SetSubtitleLayer(uint32_t layerId);

        void SetDanmakuListFromXml(winrt::hstring const& xmlStr);
        void SetSubtitleListFromJson(winrt::hstring const& jsonStr);

        void Clear();
        void ClearLayer(uint32_t layerId);

        void Close();

    private:
        void Updater_DoWork();

        // AutoResetEvent equivalent (starts unset)
        void WaitUpdateEvent();
        void SignalUpdate();

        // ManualResetEventSlim equivalent (starts set = true)
        void WaitPauseEvent();
        void SetPauseSignal(bool signaled);

        std::unique_ptr<::DanmakuFrostMasterXx::DanmakuRender>  m_render;

        std::vector<::DanmakuFrostMasterXx::DanmakuItem>        m_danmakuList;
        mutable std::recursive_mutex                             m_danmakuListMutex;

        std::queue<uint32_t>     m_updateTimeQueue;
        mutable std::mutex       m_updateTimeMutex;

        // AutoResetEvent
        std::mutex               m_updateMutex;
        std::condition_variable  m_updateCv;
        bool                     m_updateSignaled = false;

        // ManualResetEventSlim (starts SET)
        std::mutex               m_pauseMutex;
        std::condition_variable  m_pauseCv;
        bool                     m_pauseSignaled  = true;

        std::atomic<bool>        m_isClosing{ false };
        std::atomic<bool>        m_isRenderEnabled{ false };
        std::atomic<bool>        m_isSeeking{ false };
        std::atomic<bool>        m_hasSubtitle{ false };
        std::atomic<int32_t>     m_lastIndex{ 0 };
        std::atomic<uint32_t>    m_lastTimeMs{ 0 };
        std::atomic<int32_t>     m_subtitleIndexAfterSeek{ -1 };
    };
}

namespace winrt::DanmakuFrostMasterXx::factory_implementation
{
    struct DanmakuFrostMaster : DanmakuFrostMasterT<DanmakuFrostMaster, implementation::DanmakuFrostMaster>
    {
    };
}
