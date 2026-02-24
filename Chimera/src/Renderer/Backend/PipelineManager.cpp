#include "pch.h"
#include "PipelineManager.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/ShaderManager.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Utils/VulkanBarrier.h"
#include "Core/Application.h"
#include "Renderer/RenderState.h"

namespace Chimera
{
    PipelineManager* PipelineManager::s_Instance = nullptr;

    PipelineManager::PipelineManager()
    {
        s_Instance = this;
    }

    PipelineManager::~PipelineManager()
    {
        VkDevice device = VulkanContext::Get().GetDevice();

        for (auto& [name, p] : m_GraphicsCache)
        {
            if (p->handle != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, p->handle, nullptr);
            }
            // Layout handled by m_LayoutCache
        }

        for (auto& [name, p] : m_RaytracingCache)
        {
            if (p->handle != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, p->handle, nullptr);
            }
        }

        for (auto& [name, p] : m_ComputeCache)
        {
            if (p->handle != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, p->handle, nullptr);
            }
        }

        for (auto& [hash, layout] : m_LayoutCache)
        {
            vkDestroyPipelineLayout(device, layout, nullptr);
        }

        for (auto& [hash, layout] : m_Set2LayoutCache)
        {
            vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
    }

    GraphicsPipeline& PipelineManager::GetGraphicsPipeline(const std::vector<VkFormat>& colorFormats, VkFormat depthFormat, const GraphicsPipelineDescription& desc)
    {
        if (m_GraphicsCache.count(desc.name))
        {
            return *m_GraphicsCache[desc.name];
        }

        auto p = std::make_unique<GraphicsPipeline>();
        auto vSh = ShaderManager::GetShader(desc.vertex_shader);
        auto fSh = ShaderManager::GetShader(desc.fragment_shader);

        p->shaders = { vSh.get(), fSh.get() };
        p->layout = GetReflectionLayout(p->shaders);

        VkShaderModule vMod = CreateShaderModule(VulkanContext::Get().GetDevice(), vSh->GetBytecode());
        VkShaderModule fMod = CreateShaderModule(VulkanContext::Get().GetDevice(), fSh->GetBytecode());

        VkPipelineShaderStageCreateInfo ss[2] = {};
        ss[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vMod, "main" };
        ss[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fMod, "main" };

        bool isFullscreen = (desc.name == "Composition" || desc.name == "FinalBlit");
        auto bD = VertexInfo::getBindingDescription();
        auto aD = VertexInfo::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vI{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vI.vertexBindingDescriptionCount = isFullscreen ? 0 : 1;
        vI.pVertexBindingDescriptions = isFullscreen ? nullptr : &bD;
        vI.vertexAttributeDescriptionCount = isFullscreen ? 0 : (uint32_t)aD.size();
        vI.pVertexAttributeDescriptions = isFullscreen ? nullptr : aD.data();

        VkPipelineInputAssemblyStateCreateInfo iA{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
        VkPipelineViewportStateCreateInfo vP{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
        VkPipelineRasterizationStateCreateInfo rA{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f };
        VkPipelineMultisampleStateCreateInfo mS{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT };
        VkPipelineDepthStencilStateCreateInfo dS{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, desc.depth_test, desc.depth_write, VK_COMPARE_OP_GREATER_OR_EQUAL };
        
        std::vector<VkPipelineColorBlendAttachmentState> bA(colorFormats.size(), { VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT });
        VkPipelineColorBlendStateCreateInfo cB{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, (uint32_t)bA.size(), bA.data() };
        
        VkDynamicState dy[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dY{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dy };
        
        VkPipelineRenderingCreateInfo rE{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0, (uint32_t)colorFormats.size(), colorFormats.data(), depthFormat };
        VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rE, 0, 2, ss, &vI, &iA, nullptr, &vP, &rA, &mS, &dS, &cB, &dY, p->layout };

        vkCreateGraphicsPipelines(VulkanContext::Get().GetDevice(), VK_NULL_HANDLE, 1, &info, nullptr, &p->handle);
        
        vkDestroyShaderModule(VulkanContext::Get().GetDevice(), vMod, nullptr);
        vkDestroyShaderModule(VulkanContext::Get().GetDevice(), fMod, nullptr);

        m_GraphicsCache[desc.name] = std::move(p);
        return *m_GraphicsCache[desc.name];
    }

    RaytracingPipeline& PipelineManager::GetRaytracingPipeline(const RaytracingPipelineDescription& desc)
    {
        std::string key = desc.raygen_shader;
        if (m_RaytracingCache.count(key))
        {
            return *m_RaytracingCache[key];
        }

        auto p = std::make_unique<RaytracingPipeline>();
        p->shaders.push_back(ShaderManager::GetShader(desc.raygen_shader).get());
        for (const auto& m : desc.miss_shaders)
        {
            p->shaders.push_back(ShaderManager::GetShader(m).get());
        }
        for (const auto& h : desc.hit_shaders)
        {
            if (!h.closest_hit.empty())
            {
                p->shaders.push_back(ShaderManager::GetShader(h.closest_hit).get());
            }
            if (!h.any_hit.empty())
            {
                p->shaders.push_back(ShaderManager::GetShader(h.any_hit).get());
            }
        }

        p->layout = GetReflectionLayout(p->shaders);

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;

        auto addStage = [&](const Shader* sh, VkShaderStageFlagBits stageBit)
        {
            VkShaderModule mod = CreateShaderModule(VulkanContext::Get().GetDevice(), sh->GetBytecode());
            VkPipelineShaderStageCreateInfo s{};
            s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            s.stage = stageBit;
            s.module = mod;
            s.pName = "main";
            stages.push_back(s);
            return (uint32_t)stages.size() - 1;
        };

        uint32_t rgIdx = addStage(p->shaders[0], VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        VkRayTracingShaderGroupCreateInfoKHR rgGroup{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
        rgGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        rgGroup.generalShader = rgIdx;
        rgGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
        rgGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
        rgGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
        groups.push_back(rgGroup);

        for (uint32_t i = 0; i < desc.miss_shaders.size(); ++i)
        {
            uint32_t mIdx = addStage(ShaderManager::GetShader(desc.miss_shaders[i]).get(), VK_SHADER_STAGE_MISS_BIT_KHR);
            VkRayTracingShaderGroupCreateInfoKHR mGroup{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
            mGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            mGroup.generalShader = mIdx;
            mGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            mGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            mGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
            groups.push_back(mGroup);
        }

        for (uint32_t i = 0; i < desc.hit_shaders.size(); ++i)
        {
            VkRayTracingShaderGroupCreateInfoKHR hGroup{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
            hGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            hGroup.generalShader = VK_SHADER_UNUSED_KHR;
            hGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
            hGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
            hGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

            if (!desc.hit_shaders[i].closest_hit.empty())
                hGroup.closestHitShader = addStage(ShaderManager::GetShader(desc.hit_shaders[i].closest_hit).get(), VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
            if (!desc.hit_shaders[i].any_hit.empty())
                hGroup.anyHitShader = addStage(ShaderManager::GetShader(desc.hit_shaders[i].any_hit).get(), VK_SHADER_STAGE_ANY_HIT_BIT_KHR);

            groups.push_back(hGroup);
        }

        VkRayTracingPipelineCreateInfoKHR pipeInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
        pipeInfo.stageCount = (uint32_t)stages.size();
        pipeInfo.pStages = stages.data();
        pipeInfo.groupCount = (uint32_t)groups.size();
        pipeInfo.pGroups = groups.data();
        pipeInfo.maxPipelineRayRecursionDepth = 2;
        pipeInfo.layout = p->layout;

        vkCreateRayTracingPipelinesKHR(VulkanContext::Get().GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &p->handle);

        p->sbt_buffer = VulkanUtils::CreateSBT(p->handle, 1, (uint32_t)desc.miss_shaders.size(), (uint32_t)desc.hit_shaders.size(), p->sbt.raygen, p->sbt.miss, p->sbt.hit);

        for (auto& s : stages)
        {
            vkDestroyShaderModule(VulkanContext::Get().GetDevice(), s.module, nullptr);
        }

        m_RaytracingCache[key] = std::move(p);
        return *m_RaytracingCache[key];
    }

    ComputePipeline& PipelineManager::GetComputePipeline(const ComputePipelineDescription::Kernel& kernel)
    {
        std::string key = kernel.shader;
        if (m_ComputeCache.count(key))
        {
            return *m_ComputeCache[key];
        }

        auto p = std::make_unique<ComputePipeline>();
        auto sh = ShaderManager::GetShader(kernel.shader);
        p->shaders = { sh.get() };
        p->layout = GetReflectionLayout(p->shaders);

        VkShaderModule mod = CreateShaderModule(VulkanContext::Get().GetDevice(), sh->GetBytecode());
        VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        info.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, mod, "main" };
        info.layout = p->layout;

        vkCreateComputePipelines(VulkanContext::Get().GetDevice(), VK_NULL_HANDLE, 1, &info, nullptr, &p->handle);
        vkDestroyShaderModule(VulkanContext::Get().GetDevice(), mod, nullptr);

        m_ComputeCache[key] = std::move(p);
        return *m_ComputeCache[key];
    }

    VkPipelineLayout PipelineManager::GetReflectionLayout(const std::vector<const Shader*>& shaders)
    {
        size_t hash = 0;
        for (const auto* sh : shaders)
        {
            if (sh)
            {
                hash ^= std::hash<std::string>{}(sh->GetPath()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
        }

        if (m_LayoutCache.count(hash))
        {
            return m_LayoutCache[hash];
        }

        std::vector<VkDescriptorSetLayout> layouts;
        layouts.push_back(Application::Get().GetRenderState()->GetLayout());
        layouts.push_back(ResourceManager::Get().GetSceneDescriptorSetLayout());

        VkDescriptorSetLayout set2 = GetSet2Layout(shaders);
        if (set2 != VK_NULL_HANDLE)
        {
            layouts.push_back(set2);
        }

        VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        info.setLayoutCount = (uint32_t)layouts.size();
        info.pSetLayouts = layouts.data();

        VkPushConstantRange range{ VK_SHADER_STAGE_ALL, 0, 256 };
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &range;

        VkPipelineLayout layout;
        vkCreatePipelineLayout(VulkanContext::Get().GetDevice(), &info, nullptr, &layout);
        
        m_LayoutCache[hash] = layout;
        return layout;
    }

    VkDescriptorSetLayout PipelineManager::GetSet2Layout(const std::vector<const Shader*>& shaders)
    {
        std::map<uint32_t, ShaderResource> uniqueBindings;
        for (const auto* sh : shaders)
        {
            auto shBindings = sh->GetSetBindings(2);
            for (const auto& b : shBindings)
            {
                uniqueBindings[b.binding] = b;
            }
        }

        if (uniqueBindings.empty())
        {
            return VK_NULL_HANDLE;
        }

        size_t hash = 0;
        for (const auto& [binding, b] : uniqueBindings)
        {
            // Robust combine hash
            hash ^= std::hash<uint32_t>{}(b.binding) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32_t>{}(static_cast<uint32_t>(b.type)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<uint32_t>{}(b.count) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }

        if (m_Set2LayoutCache.count(hash))
        {
            return m_Set2LayoutCache[hash];
        }

        std::vector<VkDescriptorSetLayoutBinding> vkBindings;
        for (const auto& [binding, b] : uniqueBindings)
        {
            VkDescriptorSetLayoutBinding vkb{};
            vkb.binding = b.binding;
            vkb.descriptorType = b.type;
            vkb.descriptorCount = b.count;
            vkb.stageFlags = VK_SHADER_STAGE_ALL;
            vkBindings.push_back(vkb);
        }

        VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        info.bindingCount = (uint32_t)vkBindings.size();
        info.pBindings = vkBindings.data();

        VkDescriptorSetLayout layout;
        vkCreateDescriptorSetLayout(VulkanContext::Get().GetDevice(), &info, nullptr, &layout);
        m_Set2LayoutCache[hash] = layout;
        return layout;
    }

    VkShaderModule PipelineManager::CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code)
    {
        VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        info.codeSize = code.size() * sizeof(uint32_t);
        info.pCode = code.data();
        VkShaderModule mod;
        vkCreateShaderModule(device, &info, nullptr, &mod);
        return mod;
    }
}
