#pragma once

#include <cstdint>
#include <string>

namespace Chimera
{
class CaptureReadinessTracker
{
public:
    explicit CaptureReadinessTracker(uint32_t requiredStableFrames = 2)
        : m_RequiredStableFrames(requiredStableFrames)
    {
    }

    void Update(bool prerequisitesReady, const std::string& signature)
    {
        if (!prerequisitesReady || signature.empty())
        {
            m_ObservedSignature.clear();
            m_StableFrames = 0;
            return;
        }

        if (signature != m_ObservedSignature)
        {
            m_ObservedSignature = signature;
            m_StableFrames = 1;
            return;
        }

        if (m_StableFrames < m_RequiredStableFrames)
        {
            ++m_StableFrames;
        }
    }

    bool IsReadyFor(const std::string& signature) const
    {
        return !signature.empty() && signature == m_ObservedSignature &&
               m_StableFrames >= m_RequiredStableFrames;
    }

    uint32_t GetStableFrameCount() const
    {
        return m_StableFrames;
    }

    uint32_t GetRequiredStableFrameCount() const
    {
        return m_RequiredStableFrames;
    }

private:
    uint32_t m_RequiredStableFrames = 2;
    uint32_t m_StableFrames = 0;
    std::string m_ObservedSignature;
};
} // namespace Chimera
