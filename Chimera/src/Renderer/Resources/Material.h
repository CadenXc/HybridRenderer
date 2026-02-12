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
        Material(const std::string& name, const PBRMaterial& data); // [NEW]
        ~Material() = default;

        void SetAlbedo(const glm::vec4& color)
        {
            m_Data.albedo = color;
            m_Dirty = true;
        }

        void SetEmission(const glm::vec3& color)
        {
            m_Data.emission = glm::vec4(color, 1.0f);
            m_Dirty = true;
        }

        void SetEmission(const glm::vec4& color)
        {
            m_Data.emission = color;
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
        
        void SetTextureIndices(int albedo, int normal, int metalRough)
        {
            m_Data.albedoTex = albedo;
            m_Data.normalTex = normal;
            m_Data.metalRoughTex = metalRough;
            m_Dirty = true;
        }
        
        void SetAlbedoTexture(TextureHandle handle)
        {
            m_Data.albedoTex = (handle.id != 0xFFFFFFFF) ? (int)handle.id : -1;
            m_Dirty = true;
        }

        void SetNormalTexture(TextureHandle handle)
        {
            m_Data.normalTex = (handle.id != 0xFFFFFFFF) ? (int)handle.id : -1;
            m_Dirty = true;
        }

        void SetMetalRoughTexture(TextureHandle handle)
        {
            m_Data.metalRoughTex = (handle.id != 0xFFFFFFFF) ? (int)handle.id : -1;
            m_Dirty = true;
        }

        const PBRMaterial& GetData() const
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
        PBRMaterial m_Data{};
        bool m_Dirty = true;
    };
}