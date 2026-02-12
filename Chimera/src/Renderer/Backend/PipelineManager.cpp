#include "pch.h"
#include "PipelineManager.h"
#include "Core/Application.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/RenderState.h"
#include "Renderer/Backend/ShaderManager.h"

namespace Chimera
{
    PipelineManager* PipelineManager::s_Instance = nullptr;

    PipelineManager::PipelineManager() { s_Instance = this; }
    PipelineManager::~PipelineManager() { ClearCache(); s_Instance = nullptr; }

    void PipelineManager::ClearCache()
    {
        VkDevice device = VulkanContext::Get().GetDevice();
        for (auto& [name, p] : m_GraphicsCache) vkDestroyPipeline(device, p->handle, nullptr);
        for (auto& [name, p] : m_RaytracingCache) vkDestroyPipeline(device, p->handle, nullptr);
        for (auto& [name, p] : m_ComputeCache) vkDestroyPipeline(device, p->handle, nullptr);
        for (auto& [h, l] : m_LayoutCache) vkDestroyPipelineLayout(device, l, nullptr);
        for (auto& [h, s] : m_Set2LayoutCache) vkDestroyDescriptorSetLayout(device, s, nullptr);
        m_GraphicsCache.clear(); m_RaytracingCache.clear(); m_ComputeCache.clear(); m_LayoutCache.clear(); m_Set2LayoutCache.clear();
    }

    size_t PipelineManager::CalculateShaderHash(const std::vector<const Shader*>& shaders)
    {
        size_t h = 0; for (auto* s : shaders) h ^= (size_t)s + 0x9e3779b9 + (h << 6) + (h >> 2); return h;
    }

    VkDescriptorSetLayout PipelineManager::GetSet2Layout(const std::vector<const Shader*>& shaders)
    {
        size_t hash = CalculateShaderHash(shaders);
        if (m_Set2LayoutCache.count(hash)) return m_Set2LayoutCache[hash];

        std::map<uint32_t, ShaderResource> combined;
        for (auto* s : shaders) {
            auto bindings = s->GetSetBindings(2);
            for (auto& b : bindings) combined[b.binding] = b;
        }

        std::vector<VkDescriptorSetLayoutBinding> vkBindings;
        for (auto& [b, res] : combined) vkBindings.push_back({ b, res.type, 1, VK_SHADER_STAGE_ALL, nullptr });

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        if (!vkBindings.empty()) {
            VkDescriptorSetLayoutCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)vkBindings.size(), vkBindings.data() };
            vkCreateDescriptorSetLayout(VulkanContext::Get().GetDevice(), &info, nullptr, &layout);
        }
        return m_Set2LayoutCache[hash] = layout;
    }

    VkPipelineLayout PipelineManager::GetReflectionLayout(const std::vector<const Shader*>& shaders)
    {
        size_t hash = CalculateShaderHash(shaders);
        if (m_LayoutCache.count(hash)) return m_LayoutCache[hash];

        VkDescriptorSetLayout set2Layout = GetSet2Layout(shaders);
        std::vector<VkDescriptorSetLayout> setLayouts = { 
            Application::Get().GetRenderState()->GetLayout(),
            ResourceManager::Get().GetSceneDescriptorSetLayout(),
            (set2Layout != VK_NULL_HANDLE) ? set2Layout : VulkanContext::Get().GetEmptyDescriptorSetLayout()
        };

        VkPipelineLayoutCreateInfo info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)setLayouts.size(), setLayouts.data() };
        VkPushConstantRange pr{ VK_SHADER_STAGE_ALL, 0, 256 };
        info.pushConstantRangeCount = 1; info.pPushConstantRanges = &pr;

        VkPipelineLayout layout;
        vkCreatePipelineLayout(VulkanContext::Get().GetDevice(), &info, nullptr, &layout);
        return m_LayoutCache[hash] = layout;
    }

    static VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code)
    {
        VkShaderModuleCreateInfo i{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, code.size() * sizeof(uint32_t), code.data() };
        VkShaderModule sm; vkCreateShaderModule(device, &i, nullptr, &sm); return sm;
    }

    GraphicsPipeline& PipelineManager::GetGraphicsPipeline(const std::vector<VkFormat>& colorFormats, VkFormat depthFormat, const GraphicsPipelineDescription& desc)
    {
        if (m_GraphicsCache.count(desc.name)) return *m_GraphicsCache[desc.name];
        auto p = std::make_unique<GraphicsPipeline>();
        p->shaders = { ShaderManager::GetShader(desc.vertex_shader).get(), ShaderManager::GetShader(desc.fragment_shader).get() };
        p->layout = GetReflectionLayout(p->shaders);

        VkShaderModule vMod = CreateShaderModule(VulkanContext::Get().GetDevice(), p->shaders[0]->GetBytecode());
        VkShaderModule fMod = CreateShaderModule(VulkanContext::Get().GetDevice(), p->shaders[1]->GetBytecode());
        VkPipelineShaderStageCreateInfo ss[2] = {};
        ss[0].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ss[0].stage=VK_SHADER_STAGE_VERTEX_BIT; ss[0].module=vMod; ss[0].pName="main";
        ss[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; ss[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; ss[1].module=fMod; ss[1].pName="main";

        // [FIX] 只有非全屏 Pass 才提交顶点属性，彻底消除验证层报错
        auto bD = VertexInfo::getBindingDescription(); 
        auto aD = VertexInfo::getAttributeDescriptions();
        bool isFullscreen = desc.vertex_shader.find("fullscreen") != std::string::npos;

        VkPipelineVertexInputStateCreateInfo vI{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vI.vertexBindingDescriptionCount = isFullscreen ? 0 : 1;
        vI.pVertexBindingDescriptions = isFullscreen ? nullptr : &bD;
        vI.vertexAttributeDescriptionCount = isFullscreen ? 0 : (uint32_t)aD.size();
        vI.pVertexAttributeDescriptions = isFullscreen ? nullptr : aD.data();

        VkPipelineInputAssemblyStateCreateInfo iA{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,nullptr,0,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        VkPipelineViewportStateCreateInfo vP{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,nullptr,0,1,nullptr,1,nullptr};
        VkPipelineRasterizationStateCreateInfo rA{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,nullptr,0,VK_FALSE,VK_FALSE,VK_POLYGON_MODE_FILL,VK_CULL_MODE_NONE,VK_FRONT_FACE_COUNTER_CLOCKWISE,VK_FALSE,0,0,0,1.0f};
        VkPipelineMultisampleStateCreateInfo mS{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,nullptr,0,VK_SAMPLE_COUNT_1_BIT};
        VkPipelineDepthStencilStateCreateInfo dS{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,nullptr,0,desc.depth_test,desc.depth_write,VK_COMPARE_OP_GREATER_OR_EQUAL};
        std::vector<VkPipelineColorBlendAttachmentState> bA(colorFormats.size(), {VK_FALSE,VK_BLEND_FACTOR_ONE,VK_BLEND_FACTOR_ZERO,VK_BLEND_OP_ADD,VK_BLEND_FACTOR_ONE,VK_BLEND_FACTOR_ZERO,VK_BLEND_OP_ADD,VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT});
        VkPipelineColorBlendStateCreateInfo cB{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,nullptr,0,VK_FALSE,VK_LOGIC_OP_COPY,(uint32_t)bA.size(),bA.data()};
        VkDynamicState dy[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo dY{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,nullptr,0,2,dy};
        VkPipelineRenderingCreateInfo rE{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,nullptr,0,(uint32_t)colorFormats.size(),colorFormats.data(),depthFormat};
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,&rE,0,2,ss,&vI,&iA,nullptr,&vP,&rA,&mS,&dS,&cB,&dY,p->layout};
        vkCreateGraphicsPipelines(VulkanContext::Get().GetDevice(), VK_NULL_HANDLE, 1, &info, nullptr, &p->handle);
        vkDestroyShaderModule(VulkanContext::Get().GetDevice(), vMod, nullptr); vkDestroyShaderModule(VulkanContext::Get().GetDevice(), fMod, nullptr);
        m_GraphicsCache[desc.name] = std::move(p); return *m_GraphicsCache[desc.name];
    }

    RaytracingPipeline& PipelineManager::GetRaytracingPipeline(const RaytracingPipelineDescription& desc)
    {
        std::string key = desc.raygen_shader; if (m_RaytracingCache.count(key)) return *m_RaytracingCache[key];
        auto p = std::make_unique<RaytracingPipeline>();
        p->shaders.push_back(ShaderManager::GetShader(desc.raygen_shader).get());
        for (const auto& m : desc.miss_shaders) p->shaders.push_back(ShaderManager::GetShader(m).get());
        for (const auto& h : desc.hit_shaders) { p->shaders.push_back(ShaderManager::GetShader(h.closest_hit).get()); if(!h.any_hit.empty()) p->shaders.push_back(ShaderManager::GetShader(h.any_hit).get()); }
        p->layout = GetReflectionLayout(p->shaders);

        std::vector<VkPipelineShaderStageCreateInfo> stages; std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
        auto addS = [&](const Shader* sh, VkShaderStageFlagBits st) { VkShaderModule mod = CreateShaderModule(VulkanContext::Get().GetDevice(), sh->GetBytecode()); VkPipelineShaderStageCreateInfo s{}; s.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; s.stage=st; s.module=mod; s.pName="main"; stages.push_back(s); return (uint32_t)stages.size()-1; };
        
        uint32_t sIdx = 0;
        { VkRayTracingShaderGroupCreateInfoKHR rg{}; rg.sType=VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR; rg.type=VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR; rg.generalShader=addS(p->shaders[sIdx++],VK_SHADER_STAGE_RAYGEN_BIT_KHR); rg.closestHitShader=rg.anyHitShader=rg.intersectionShader=VK_SHADER_UNUSED_KHR; groups.push_back(rg); }
        for (const auto& m : desc.miss_shaders) { VkRayTracingShaderGroupCreateInfoKHR mg{}; mg.sType=VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR; mg.type=VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR; mg.generalShader=addS(p->shaders[sIdx++],VK_SHADER_STAGE_MISS_BIT_KHR); mg.closestHitShader=mg.anyHitShader=mg.intersectionShader=VK_SHADER_UNUSED_KHR; groups.push_back(mg); }
        for (const auto& h : desc.hit_shaders) { VkRayTracingShaderGroupCreateInfoKHR hg{}; hg.sType=VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR; hg.type=VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR; hg.generalShader=VK_SHADER_UNUSED_KHR; hg.closestHitShader=addS(p->shaders[sIdx++],VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR); hg.anyHitShader=(h.any_hit.empty()?VK_SHADER_UNUSED_KHR:addS(p->shaders[sIdx++],VK_SHADER_STAGE_ANY_HIT_BIT_KHR)); hg.intersectionShader=VK_SHADER_UNUSED_KHR; groups.push_back(hg); }

        VkRayTracingPipelineCreateInfoKHR info{}; info.sType=VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR; info.stageCount=(uint32_t)stages.size(); info.pStages=stages.data(); info.groupCount=(uint32_t)groups.size(); info.pGroups=groups.data(); info.maxPipelineRayRecursionDepth=2; info.layout=p->layout;
        auto vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(VulkanContext::Get().GetDevice(), "vkCreateRayTracingPipelinesKHR");
        vkCreateRayTracingPipelinesKHR(VulkanContext::Get().GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info, nullptr, &p->handle);

        auto vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(VulkanContext::Get().GetDevice(), "vkGetRayTracingShaderGroupHandlesKHR");
        uint32_t hS = VulkanContext::Get().GetRayTracingProperties().shaderGroupHandleSize, hA = VulkanContext::Get().GetRayTracingProperties().shaderGroupBaseAlignment, nG = (uint32_t)groups.size();
        std::vector<uint8_t> hD(nG * hS); vkGetRayTracingShaderGroupHandlesKHR(VulkanContext::Get().GetDevice(), p->handle, 0, nG, hD.size(), hD.data());
        p->sbt_buffer = std::make_unique<Buffer>(nG * hA, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        uint8_t* pD = (uint8_t*)p->sbt_buffer->Map(); for(uint32_t i=0; i<nG; ++i) memcpy(pD + i*hA, hD.data() + i*hS, hS); p->sbt_buffer->Unmap();
        VkDeviceAddress sA = p->sbt_buffer->GetDeviceAddress(); p->sbt.raygen={sA,hA,hA}; p->sbt.miss={sA+hA,hA,hA}; p->sbt.hit={sA+2*hA,hA,hA};
        for (auto& s : stages) vkDestroyShaderModule(VulkanContext::Get().GetDevice(), s.module, nullptr);
        m_RaytracingCache[key] = std::move(p); return *m_RaytracingCache[key];
    }

    ComputePipeline& PipelineManager::GetComputePipeline(const ComputePipelineDescription::Kernel& kernel)
    {
        if (m_ComputeCache.count(kernel.name)) return *m_ComputeCache[kernel.name];
        auto p = std::make_unique<ComputePipeline>();
        p->shaders = { ShaderManager::GetShader(kernel.shader).get() };
        p->layout = GetReflectionLayout(p->shaders);
        VkShaderModule mod = CreateShaderModule(VulkanContext::Get().GetDevice(), p->shaders[0]->GetBytecode());
        VkComputePipelineCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO; info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,mod,"main"}; info.layout=p->layout;
        vkCreateComputePipelines(VulkanContext::Get().GetDevice(), VK_NULL_HANDLE, 1, &info, nullptr, &p->handle);
        vkDestroyShaderModule(VulkanContext::Get().GetDevice(), mod, nullptr);
        m_ComputeCache[kernel.name] = std::move(p); return *m_ComputeCache[kernel.name];
    }
}
