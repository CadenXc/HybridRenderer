#include "pch.h"
#include "Material.h"

namespace Chimera
{
Material::Material(const std::string& name) : m_Name(name)
{
    m_Data.colour = glm::vec3(1.0f);
    m_Data.emission = glm::vec3(0.0f);
    m_Data.roughness = 1.0f;
    m_Data.metallic = 0.0f;
    m_Data.materialType = (float)MATERIAL_TYPE_PBR;
    m_Data.opacity = 1.0f;
    m_Data.transmissionDepth = 0.01f;
    m_Data.colourTexture = -1;
    m_Data.normalTexture = -1;
    m_Data.roughnessTexture = -1;
    m_Data.emissionTexture = -1;
}

Material::Material(const std::string& name, const GpuMaterial& data)
    : m_Name(name), m_Data(data)
{
}
} // namespace Chimera
