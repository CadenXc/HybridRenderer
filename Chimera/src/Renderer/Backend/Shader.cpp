#include "pch.h"
#include "Shader.h"
#include <spirv_reflect.h>
#include <fstream>
#include <stdexcept>
#include <filesystem>

namespace Chimera
{
Shader::Shader(const std::filesystem::path& path)
{
    m_Path = path.string();
    m_Name = path.stem().string();

        // CRITICAL: Open file using the path object directly
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        CH_CORE_ERROR("Shader: FAILED TO OPEN FILE. Normalized path: {0}",
                      m_Path);
        throw std::runtime_error(
            "Required shader file missing or unreadable: " + m_Path);
    }

    size_t fileSize = (size_t)file.tellg();
    if (fileSize == 0 || fileSize == (size_t)-1)
    {
        CH_CORE_ERROR("Shader: File is EMPTY: {0}", m_Path);
        throw std::runtime_error("Empty or invalid shader file: " + m_Path);
    }

    m_Bytecode.resize(fileSize / 4);
    file.seekg(0);
    file.read((char*)m_Bytecode.data(), fileSize);
    file.close();

    Reflect();
}

Shader::~Shader() {}

void Shader::Reflect()
{
    if (m_Bytecode.empty())
    {
        return;
    }

    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule(
        m_Bytecode.size() * 4, m_Bytecode.data(), &module);

    if (result != SPV_REFLECT_RESULT_SUCCESS)
    {
        CH_CORE_ERROR("Shader: SPIR-V Reflection FAILED for {0}", m_Name);
        return;
    }

    uint32_t count = 0;
    spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    std::vector<SpvReflectDescriptorBinding*> bindings(count);
    spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

    for (auto* b : bindings)
    {
        ShaderResource res;
        std::string rawName = b->name;

        std::string cleanName = rawName;

        if (cleanName.find("rt") == 0 && cleanName.size() > 2 &&
            isupper(cleanName[2]))
        {
            cleanName = cleanName.substr(2);
        }
        else if (cleanName.find("g") == 0 && cleanName.size() > 1 &&
                 isupper(cleanName[1]))
        {
            cleanName = cleanName.substr(1);
        }

        res.name = cleanName;
        res.set = b->set;
        res.binding = b->binding;
        res.type = (VkDescriptorType)b->descriptor_type;
        res.count = b->count;
        m_ReflectionData[res.name] = res;
    }

    spvReflectDestroyShaderModule(&module);
}

std::vector<ShaderResource> Shader::GetSetBindings(uint32_t setIndex) const
{
    std::vector<ShaderResource> result;

    for (const auto& [name, res] : m_ReflectionData)
    {
        if (res.set == setIndex)
        {
            result.push_back(res);
        }
    }

    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b)
              { return a.binding < b.binding; });

    return result;
}
} // namespace Chimera
