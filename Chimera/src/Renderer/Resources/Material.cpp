#include "pch.h"
#include "Material.h"

namespace Chimera
{
    Material::Material(const std::string& name)
        : m_Name(name)
    {
        m_Data.albedo = glm::vec4(1.0f);
        m_Data.emission = glm::vec4(0.0f);
        m_Data.roughness = 1.0f;
        m_Data.metallic = 0.0f;
        m_Data.albedoTex = -1;
        m_Data.normalTex = -1;
        m_Data.metalRoughTex = -1;
    }

    // [NEW] 支持从 POD 结构构造
    Material::Material(const std::string& name, const PBRMaterial& data)
        : m_Name(name), m_Data(data)
    {
    }
}