#pragma once

#include <deque>
#include <functional>
#include <vector>

namespace Chimera
{
    /**
     * @brief DeletionQueue manages deferred destruction of Vulkan resources.
     * Upgraded to be frame-aware to support multi-buffering (triple buffering).
     */
    class DeletionQueue
    {
    public:
        void Init(uint32_t maxFrames)
        {
            m_FrameDeletions.resize(maxFrames);
        }

        void PushFunction(uint32_t frameIndex, std::function<void()>&& function)
        {
            m_FrameDeletions[frameIndex].push_back(std::move(function));
        }

        void PushFunction(std::function<void()>&& function)
        {
            m_GlobalDeletions.push_back(std::move(function));
        }

        void FlushFrame(uint32_t frameIndex)
        {
            auto& deletors = m_FrameDeletions[frameIndex];
            for (auto it = deletors.rbegin(); it != deletors.rend(); ++it)
            {
                (*it)();
            }
            deletors.clear();
        }

        void FlushAll()
        {
            for (uint32_t i = 0; i < (uint32_t)m_FrameDeletions.size(); i++)
            {
                FlushFrame(i);
            }
            
            for (auto it = m_GlobalDeletions.rbegin(); it != m_GlobalDeletions.rend(); ++it)
            {
                (*it)();
            }
            m_GlobalDeletions.clear();
        }

    private:
        std::vector<std::deque<std::function<void()>>> m_FrameDeletions;
        std::deque<std::function<void()>> m_GlobalDeletions;
    };
}