#pragma once
#include <cstdint>
#include <vector>
#include <mutex>
#include <random>

namespace DanmakuFrostMasterXx
{
    // Thread-safe via internal mutex.
    class DanmakuYSlotManager
    {
    public:
        explicit DanmakuYSlotManager(uint32_t length)
            : m_random(std::random_device{}())
        {
            m_ySlotArray.resize(length);
        }

        // Not copyable
        DanmakuYSlotManager(const DanmakuYSlotManager&) = delete;
        DanmakuYSlotManager& operator=(const DanmakuYSlotManager&) = delete;

        void UpdateLength(uint32_t newLength)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_ySlotArray.assign(newLength, Slot{});
        }

        // Returns true if a free slot was found; false = random fallback y.
        bool GetY(uint32_t danmakuId, uint32_t height, uint32_t& outY)
        {
            const uint32_t arrayLen = static_cast<uint32_t>(m_ySlotArray.size());
            if (height > arrayLen)
            {
                outY = 0;
                return false;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            uint32_t index = 0;
            while (index + height <= arrayLen)
            {
                bool found = true;
                for (uint32_t i = 0; i < height; ++i)
                {
                    if (m_ySlotArray[index + i].Length > 0)
                    {
                        found = false;
                        index = index + i + m_ySlotArray[index + i].Length;
                        break;
                    }
                }
                if (found)
                {
                    m_ySlotArray[index].Id     = danmakuId;
                    m_ySlotArray[index].Length = height;
                    outY = index;
                    return true;
                }
            }

            // No free slot – return a random Y position
            const uint32_t maxStart = (arrayLen > height) ? (arrayLen - height) : 0u;
            std::uniform_int_distribution<uint32_t> dist(0, maxStart);
            outY = dist(m_random);
            return false;
        }

        void ReleaseYSlot(uint32_t danmakuId, uint32_t y) noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const uint32_t arrayLen = static_cast<uint32_t>(m_ySlotArray.size());
            if (y < arrayLen && m_ySlotArray[y].Id == danmakuId)
            {
                m_ySlotArray[y].Id     = 0;
                m_ySlotArray[y].Length = 0;
            }
        }

        void Clear() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& slot : m_ySlotArray)
            {
                slot.Id     = 0;
                slot.Length = 0;
            }
        }

    private:
        struct Slot
        {
            uint32_t Id     = 0;
            uint32_t Length = 0;
        };

        std::vector<Slot>  m_ySlotArray;
        mutable std::mutex m_mutex;
        std::mt19937       m_random;
    };
}
