#include "pch.h"
#include "PipelineManager.h"
#include "Core/Application.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/RenderState.h"
#include "Renderer/Backend/ShaderMetadata.h"
#include "Renderer/Backend/ShaderManager.h"
#include "Utils/VulkanBarrier.h"
#include <fstream>

namespace Chimera
{
    PipelineManager* PipelineManager::s_Instance = nullptr;

    PipelineManager::PipelineManager()
    {
        s_Instance = this;
    }

    PipelineManager::~PipelineManager()
    {
        ClearCache();
        s_Instance = nullptr;
    }

    void PipelineManager::ClearCache()
    {
        VkDevice device = VulkanContext::Get().GetDevice();
        for (auto& [name, p] : m_GraphicsCache)
        {
            vkDestroyPipeline(device, p->handle, nullptr);
            vkDestroyPipelineLayout(device, p->layout, nullptr);
        }
        for (auto& [name, p] : m_RaytracingCache)
        {
            vkDestroyPipeline(device, p->handle, nullptr);
            vkDestroyPipelineLayout(device, p->layout, nullptr);
        }
        for (auto& [name, p] : m_ComputeCache)
        {
            vkDestroyPipeline(device, p->handle, nullptr);
            vkDestroyPipelineLayout(device, p->layout, nullptr);
        }
        m_GraphicsCache.clear();
        m_RaytracingCache.clear();
        m_ComputeCache.clear();
    }

    static VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code)
    {
        VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();
        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create shader module!");
        }
        return shaderModule;
    }

    GraphicsPipeline& PipelineManager::GetGraphicsPipeline(const RenderPass& renderPass, const GraphicsPipelineDescription& description)
    {
        if (m_GraphicsCache.count(description.name))
        {
            return *m_GraphicsCache[description.name];
        }
        
        CH_CORE_INFO("PipelineManager: Creating Graphics Pipeline '{0}'...", description.name);
        auto pipeline = std::make_unique<GraphicsPipeline>();
        pipeline->description = description;

        // Standard Layout Contract: 0=Global, 1=Scene, 2=Pass
        std::vector<VkDescriptorSetLayout> setLayouts = { 
            Application::Get().GetRenderState()->GetLayout(),
            ResourceManager::Get().GetSceneDescriptorSetLayout(),
            (renderPass.descriptor_set_layout != VK_NULL_HANDLE) ? renderPass.descriptor_set_layout : VulkanContext::Get().GetEmptyDescriptorSetLayout()
        };

        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = (uint32_t)setLayouts.size();
        layoutInfo.pSetLayouts = setLayouts.data();
        
        VkPushConstantRange pr{};
        pr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pr.offset = 0;
        pr.size = 256; 
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pr;

        if (vkCreatePipelineLayout(VulkanContext::Get().GetDevice(), &layoutInfo, nullptr, &pipeline->layout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        VkShaderModule vertModule = CreateShaderModule(VulkanContext::Get().GetDevice(), ShaderManager::GetShader(description.vertex_shader)->GetBytecode());
        VkShaderModule fragModule = CreateShaderModule(VulkanContext::Get().GetDevice(), ShaderManager::GetShader(description.fragment_shader)->GetBytecode());

        VkPipelineShaderStageCreateInfo vertStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";
        VkPipelineShaderStageCreateInfo fragStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";
        VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        auto bindingDescription = VertexInfo::getBindingDescription();
        auto attributeDescriptions = VertexInfo::getAttributeDescriptions();
        if (description.vertex_shader.find("fullscreen") == std::string::npos)
        {
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
            vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)attributeDescriptions.size();
            vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = description.cull_mode;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthStencil.depthTestEnable = description.depth_test ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = description.depth_write ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
        const GraphicsPass& gp = std::get<GraphicsPass>(renderPass.pass);
        for (auto& att : gp.attachments)
        {
            if (VulkanUtils::IsDepthFormat(att.image.format))
            {
                continue;
            }
            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;
            blendAttachments.push_back(colorBlendAttachment);
        }
        VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        colorBlending.attachmentCount = (uint32_t)blendAttachments.size();
        colorBlending.pAttachments = blendAttachments.data();
        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        std::vector<VkFormat> colorFormats;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        for (auto& att : gp.attachments)
        {
            if (VulkanUtils::IsDepthFormat(att.image.format))
            {
                depthFormat = att.image.format;
            }
            else
            {
                colorFormats.push_back(att.image.format);
            }
        }
        VkPipelineRenderingCreateInfo renderingCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        renderingCreateInfo.colorAttachmentCount = (uint32_t)colorFormats.size();
        renderingCreateInfo.pColorAttachmentFormats = colorFormats.data();
        renderingCreateInfo.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineInfo.pNext = &renderingCreateInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipeline->layout;

        if (vkCreateGraphicsPipelines(VulkanContext::Get().GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline->handle) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create graphics pipeline!");
        }
        VulkanContext::Get().SetDebugName((uint64_t)pipeline->handle, VK_OBJECT_TYPE_PIPELINE, description.name.c_str());
        VulkanContext::Get().SetDebugName((uint64_t)pipeline->layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (description.name + "_Layout").c_str());
        
        vkDestroyShaderModule(VulkanContext::Get().GetDevice(), vertModule, nullptr);
        vkDestroyShaderModule(VulkanContext::Get().GetDevice(), fragModule, nullptr);
        m_GraphicsCache[description.name] = std::move(pipeline);
        return *m_GraphicsCache[description.name];
    }

    RaytracingPipeline& PipelineManager::GetRaytracingPipeline(const RenderPass& renderPass, const RaytracingPipelineDescription& description)
    {
        std::string key = description.raygen_shader; 
        if (m_RaytracingCache.count(key))
        {
            return *m_RaytracingCache[key];
        }
        
        CH_CORE_INFO("PipelineManager: Creating Raytracing Pipeline '{0}'...", key);
        auto pipeline = std::make_unique<RaytracingPipeline>();
        pipeline->description = description;

        std::vector<VkDescriptorSetLayout> setLayouts = { 
            Application::Get().GetRenderState()->GetLayout(),
            ResourceManager::Get().GetSceneDescriptorSetLayout(),
            (renderPass.descriptor_set_layout != VK_NULL_HANDLE) ? renderPass.descriptor_set_layout : VulkanContext::Get().GetEmptyDescriptorSetLayout()
        };

        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = (uint32_t)setLayouts.size();
        layoutInfo.pSetLayouts = setLayouts.data();
        VkPushConstantRange pr{};
        pr.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
        pr.offset = 0;
        pr.size = 256;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pr;
        vkCreatePipelineLayout(VulkanContext::Get().GetDevice(), &layoutInfo, nullptr, &pipeline->layout);

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
        auto addStage = [&](const std::string& name, VkShaderStageFlagBits stage)
        {
            auto shader = ShaderManager::GetShader(name);
            VkShaderModule module = CreateShaderModule(VulkanContext::Get().GetDevice(), shader->GetBytecode());
            VkPipelineShaderStageCreateInfo s{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            s.stage = stage;
            s.module = module;
            s.pName = "main";
            stages.push_back(s);
            return (uint32_t)stages.size() - 1;
        };

        uint32_t rIdx = addStage(description.raygen_shader, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        VkRayTracingShaderGroupCreateInfoKHR rg{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
        rg.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        rg.generalShader = rIdx;
        rg.closestHitShader = rg.anyHitShader = rg.intersectionShader = VK_SHADER_UNUSED_KHR;
        groups.push_back(rg);

        for (const auto& m : description.miss_shaders)
        {
            uint32_t mIdx = addStage(m, VK_SHADER_STAGE_MISS_BIT_KHR);
            VkRayTracingShaderGroupCreateInfoKHR mg{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
            mg.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            mg.generalShader = mIdx;
            mg.closestHitShader = mg.anyHitShader = mg.intersectionShader = VK_SHADER_UNUSED_KHR;
            groups.push_back(mg);
        }
        for (const auto& h : description.hit_shaders)
        {
            VkRayTracingShaderGroupCreateInfoKHR hg{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
            hg.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            hg.generalShader = VK_SHADER_UNUSED_KHR;
            hg.closestHitShader = addStage(h.closest_hit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
            hg.anyHitShader = h.any_hit.empty() ? VK_SHADER_UNUSED_KHR : addStage(h.any_hit, VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
            hg.intersectionShader = VK_SHADER_UNUSED_KHR;
            groups.push_back(hg);
        }

        VkRayTracingPipelineCreateInfoKHR pipelineInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
        pipelineInfo.stageCount = (uint32_t)stages.size();
        pipelineInfo.pStages = stages.data();
        pipelineInfo.groupCount = (uint32_t)groups.size();
        pipelineInfo.pGroups = groups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = 2;
        pipelineInfo.layout = pipeline->layout;
        auto vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(VulkanContext::Get().GetDevice(), "vkCreateRayTracingPipelinesKHR");
        vkCreateRayTracingPipelinesKHR(VulkanContext::Get().GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline->handle);
        VulkanContext::Get().SetDebugName((uint64_t)pipeline->handle, VK_OBJECT_TYPE_PIPELINE, key.c_str());
        VulkanContext::Get().SetDebugName((uint64_t)pipeline->layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, (key + "_Layout").c_str());

        auto vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(VulkanContext::Get().GetDevice(), "vkGetRayTracingShaderGroupHandlesKHR");
        uint32_t hSize = VulkanContext::Get().GetRayTracingProperties().shaderGroupHandleSize;
        uint32_t hAlign = VulkanContext::Get().GetRayTracingProperties().shaderGroupBaseAlignment;
        uint32_t nGroups = (uint32_t)groups.size();
        std::vector<uint8_t> hData(nGroups * hSize);
        vkGetRayTracingShaderGroupHandlesKHR(VulkanContext::Get().GetDevice(), pipeline->handle, 0, nGroups, hData.size(), hData.data());
        auto sbtBuffer = std::make_unique<Buffer>(nGroups * hAlign, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        uint8_t* pD = (uint8_t*)sbtBuffer->Map();
        for(uint32_t i = 0; i < nGroups; ++i)
        {
            memcpy(pD + i * hAlign, hData.data() + i * hSize, hSize);
        }
        sbtBuffer->Unmap();
        VkDeviceAddress sAddr = sbtBuffer->GetDeviceAddress();
        pipeline->sbt.raygen = { sAddr, hAlign, hAlign };
        pipeline->sbt.miss = { sAddr + hAlign, hAlign, hAlign };
        pipeline->sbt.hit = { sAddr + 2 * hAlign, hAlign, hAlign };
        pipeline->sbt_buffer = std::move(sbtBuffer);
        for (auto& s : stages)
        {
            vkDestroyShaderModule(VulkanContext::Get().GetDevice(), s.module, nullptr);
        }
        m_RaytracingCache[key] = std::move(pipeline);
        return *m_RaytracingCache[key];
    }

    ComputePipeline& PipelineManager::GetComputePipeline(const RenderPass& renderPass, const ComputePipelineDescription::Kernel& kernel)
    {
        if (m_ComputeCache.count(kernel.name))
        {
            return *m_ComputeCache[kernel.name];
        }
        CH_CORE_INFO("PipelineManager: Creating Compute Pipeline '{0}'...", kernel.name);
        auto pipeline = std::make_unique<ComputePipeline>();
        
        std::vector<VkDescriptorSetLayout> setLayouts = { 
            Application::Get().GetRenderState()->GetLayout(), 
            ResourceManager::Get().GetSceneDescriptorSetLayout(),
            (renderPass.descriptor_set_layout != VK_NULL_HANDLE) ? renderPass.descriptor_set_layout : VulkanContext::Get().GetEmptyDescriptorSetLayout()
        };

        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = (uint32_t)setLayouts.size();
        layoutInfo.pSetLayouts = setLayouts.data();
        VkPushConstantRange pr{};
        pr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pr.offset = 0;
        pr.size = 256;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pr;
        vkCreatePipelineLayout(VulkanContext::Get().GetDevice(), &layoutInfo, nullptr, &pipeline->layout);
        auto shader = ShaderManager::GetShader(kernel.shader);
        VkShaderModule module = CreateShaderModule(VulkanContext::Get().GetDevice(), shader->GetBytecode());
        VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = pipeline->layout;
        vkCreateComputePipelines(VulkanContext::Get().GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline->handle);
        vkDestroyShaderModule(VulkanContext::Get().GetDevice(), module, nullptr);
        m_ComputeCache[kernel.name] = std::move(pipeline);
        return *m_ComputeCache[kernel.name];
    }
}