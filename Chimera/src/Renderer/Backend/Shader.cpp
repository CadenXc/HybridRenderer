#include "pch.h"
#include "Shader.h"
#include <spirv_reflect.h>
#include <fstream>

namespace Chimera
{
    Shader::Shader(const std::string& path) : m_Path(path)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        
        // [FIX] 增加安全检查，防止 bad_array_new_length 闪退
        if (!file.is_open())
        {
            CH_CORE_ERROR("Shader: Failed to open SPV file at path: {0}", path);
            // 填充一个最小的有效字节码以防止后续流程崩溃
            m_Bytecode = { 0x07230203 }; 
            return;
        }

        size_t fileSize = (size_t)file.tellg();
        if (fileSize == 0 || fileSize == (size_t)-1)
        {
            CH_CORE_ERROR("Shader: SPV file is empty or invalid: {0}", path);
            return;
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
        if (m_Bytecode.empty()) return;

        SpvReflectShaderModule module;
        SpvReflectResult result = spvReflectCreateShaderModule(m_Bytecode.size() * 4, m_Bytecode.data(), &module);
        if (result != SPV_REFLECT_RESULT_SUCCESS) return;
        
        uint32_t count = 0;
        spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
        std::vector<SpvReflectDescriptorBinding*> bindings(count);
        spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

        for (auto* b : bindings)
        {
            ShaderResource res;
            std::string rawName = b->name;
            
            // 增强版名称清理逻辑
            std::string cleanName = rawName;
            if (cleanName.find("rt") == 0 && cleanName.size() > 2 && isupper(cleanName[2])) cleanName = cleanName.substr(2);
            else if (cleanName.find("g") == 0 && cleanName.size() > 1 && isupper(cleanName[1])) cleanName = cleanName.substr(1);
            
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
            if (res.set == setIndex) result.push_back(res);
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.binding < b.binding; });
        return result;
    }
}