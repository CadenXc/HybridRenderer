#pragma once

#include "pch.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace Chimera
{
struct ShaderResource
{
    std::string name;
    uint32_t set;
    uint32_t binding;
    VkDescriptorType type;
    uint32_t count;
};

struct ShaderPushConstantInfo
{
    uint32_t offset = 0;
    uint32_t size = 0;
    VkShaderStageFlags stages = 0;

    bool IsValid() const
    {
        return size > 0 && stages != 0;
    }
};

class Shader
{
public:
    Shader(const std::filesystem::path& path);
    ~Shader();

    const std::string& GetPath() const
    {
        return m_Path;
    }
    const std::string& GetName() const
    {
        return m_Name;
    }
    const std::vector<uint32_t>& GetBytecode() const
    {
        return m_Bytecode;
    }
    const std::unordered_map<std::string, ShaderResource>& GetReflectionData()
        const
    {
        return m_ReflectionData;
    }

    const ShaderPushConstantInfo& GetPushConstantInfo() const
    {
        return m_PushConstantInfo;
    }

        // [MODERN] 获取 Set 2 的所有绑定点，按 Binding 排序
    std::vector<ShaderResource> GetSetBindings(uint32_t setIndex) const;

private:
    void Reflect();

private:
    std::string m_Path;
    std::string m_Name;
    std::vector<uint32_t> m_Bytecode;
    std::unordered_map<std::string, ShaderResource> m_ReflectionData;
    ShaderPushConstantInfo m_PushConstantInfo;
};
} // namespace Chimera
