#pragma once

#include <cstdint>

namespace Chimera
{
class FrameWarmupCounter
{
public:
    void Start(uint32_t frameCount)
    {
        m_RemainingFrames = frameCount;
    }

    bool AdvanceAfterRenderedFrame()
    {
        if (m_RemainingFrames == 0)
        {
            return false;
        }

        --m_RemainingFrames;
        return m_RemainingFrames == 0;
    }

    bool IsActive() const
    {
        return m_RemainingFrames != 0;
    }

    uint32_t GetRemainingFrames() const
    {
        return m_RemainingFrames;
    }

private:
    uint32_t m_RemainingFrames = 0;
};
} // namespace Chimera
