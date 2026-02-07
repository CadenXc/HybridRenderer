#include "pch.h"
#include "Material.h"

namespace Chimera {

    Material::Material(const std::string& name)
        : m_Name(name)
    {
        m_Data.albedo = glm::vec4(1.0f);
        m_Data.emission = glm::vec3(0.0f);
        m_Data.roughness = 0.5f;
        m_Data.metallic = 0.0f;
        m_Data.albedoTex = TextureHandle(0);
        m_Data.normalTex = TextureHandle(0);
        m_Data.metalRoughTex = TextureHandle(0);
    }

}
