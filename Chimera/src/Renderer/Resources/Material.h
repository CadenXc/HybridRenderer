#pragma once
#include "pch.h"
#include "Renderer/Resources/ResourceHandle.h"
#include "Renderer/Backend/ShaderCommon.h"

namespace Chimera
{
class Material
{
public:
    Material(const std::string& name = "New Material");
    Material(const std::string& name, const GpuMaterial& data);
    ~Material() = default;

    void SetColour(const glm::vec3& color)
    {
        m_Data.colour = color;
        m_Dirty = true;
    }

    void SetEmission(const glm::vec3& color)
    {
        m_Data.emission = color;
        m_Dirty = true;
    }

    void SetData(const GpuMaterial& data)
    {
        m_Data = data;
        m_Dirty = true;
    }

    void SetRoughness(float r)
    {
        m_Data.roughness = r;
        m_Dirty = true;
    }

    void SetMetallic(float m)
    {
        m_Data.metallic = m;
        m_Dirty = true;
    }

    void SetTextureIndices(int colour, int normal, int roughness)
    {
        m_Data.colourTexture = colour;
        m_Data.normalTexture = normal;
        m_Data.roughnessTexture = roughness;
        m_Dirty = true;
    }

    void SetColourTexture(TextureHandle handle)
    {
        m_Data.colourTexture = (handle.id != 0xFFFFFFFF) ? (int)handle.id : -1;
        m_Dirty = true;
    }

    void SetNormalTexture(TextureHandle handle)
    {
        m_Data.normalTexture = (handle.id != 0xFFFFFFFF) ? (int)handle.id : -1;
        m_Dirty = true;
    }

    void SetRoughnessTexture(TextureHandle handle)
    {
        m_Data.roughnessTexture =
            (handle.id != 0xFFFFFFFF) ? (int)handle.id : -1;
        m_Dirty = true;
    }

    const GpuMaterial& GetData() const
    {
        return m_Data;
    }

    const std::string& GetName() const
    {
        return m_Name;
    }

    bool IsDirty() const
    {
        return m_Dirty;
    }

    void ClearDirty()
    {
        m_Dirty = false;
    }

private:
    std::string m_Name;
    GpuMaterial m_Data{};
    bool m_Dirty = true;
};
} // namespace Chimera
