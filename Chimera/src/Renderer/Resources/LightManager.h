#pragma once

#include "pch.h"
#include "Renderer/Backend/ShaderCommon.h"
#include <vector>
#include <memory>

namespace Chimera
{
class Scene;
class Buffer;

/**
 * @brief Manages emissive objects and environment lights for importance
 * sampling. Based on SVGF reference implementation.
 */
class LightManager
{
public:
    LightManager();
    ~LightManager();

    void Build(Scene* scene);

    Buffer* GetLightBuffer() const
    {
        return m_LightBuffer.get();
    }
    Buffer* GetCDFBuffer() const
    {
        return m_CDFBuffer.get();
    }
    uint32_t GetLightCount() const
    {
        return (uint32_t)m_GpuLights.size();
    }

private:
    std::vector<GpuLight> m_GpuLights;
    std::vector<float> m_LightsCDF;

    std::unique_ptr<Buffer> m_LightBuffer;
    std::unique_ptr<Buffer> m_CDFBuffer;
};
} // namespace Chimera
