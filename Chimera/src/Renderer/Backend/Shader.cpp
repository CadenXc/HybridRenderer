#include "pch.h"
#include "Shader.h"
#include "spirv_reflect.h"
#include "Core/FileIO.h"

namespace Chimera
{
    Shader::Shader(const std::string& path)
        : m_Path(path)
    {
        auto data = FileIO::ReadFile(path);
        CH_CORE_INFO("Shader: Read {0} bytes from {1}", data.size(), path);
        m_Bytecode.resize(data.size() / 4);
        memcpy(m_Bytecode.data(), data.data(), data.size());

        Reflect();
    }

    Shader::~Shader()
    {
    }

    static VkDescriptorType ReflectToVulkanDescriptorType(SpvReflectDescriptorType type)
    {
        switch (type)
        {
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:                return VK_DESCRIPTOR_TYPE_SAMPLER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:          return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:          return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:   return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:   return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:         return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:         return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
            case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:       return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        }
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }

    void Shader::Reflect()
    {
        SpvReflectShaderModule module;
        SpvReflectResult result = spvReflectCreateShaderModule(m_Bytecode.size() * 4, m_Bytecode.data(), &module);
        if (result != SPV_REFLECT_RESULT_SUCCESS)
        {
            CH_CORE_ERROR("Shader Reflection Failed: {0}", m_Path);
            return;
        }

        m_Layout.name = m_Path;

        // 1. Reflect Descriptor Sets
        uint32_t count = 0;
        spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
        std::vector<SpvReflectDescriptorBinding*> bindings(count);
        spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

        for (auto* b : bindings)
        {
            ShaderResourceBinding binding{};
            binding.name = b->name;
            binding.binding = b->binding;
            binding.type = ReflectToVulkanDescriptorType(b->descriptor_type);
            binding.count = b->count;
            binding.stage = (VkShaderStageFlags)module.shader_stage;

            m_Layout.resources[binding.name] = binding;
            m_Layout.bindings[binding.binding] = binding;
        }

        // 2. Reflect Push Constants
        uint32_t pc_count = 0;
        spvReflectEnumeratePushConstantBlocks(&module, &pc_count, nullptr);
        std::vector<SpvReflectBlockVariable*> pc_blocks(pc_count);
        spvReflectEnumeratePushConstantBlocks(&module, &pc_count, pc_blocks.data());

        if (pc_count > 0)
        {
            m_PushConstantSize = pc_blocks[0]->size;
        }

        spvReflectDestroyShaderModule(&module);
    }
}
