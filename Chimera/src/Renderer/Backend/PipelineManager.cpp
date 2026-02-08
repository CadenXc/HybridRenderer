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

namespace Chimera {

    PipelineManager::PipelineManager(std::shared_ptr<VulkanContext> context, ResourceManager& resourceManager)
        : m_Context(context), m_ResourceManager(resourceManager) {}

    PipelineManager::~PipelineManager() { ClearCache(); }

    void PipelineManager::ClearCache() {
        VkDevice device = m_Context->GetDevice();
        for (auto& [name, p] : m_GraphicsCache) { vkDestroyPipeline(device, p->handle, nullptr); vkDestroyPipelineLayout(device, p->layout, nullptr); }
        for (auto& [name, p] : m_RaytracingCache) { vkDestroyPipeline(device, p->handle, nullptr); vkDestroyPipelineLayout(device, p->layout, nullptr); }
        for (auto& [name, p] : m_ComputeCache) { vkDestroyPipeline(device, p->handle, nullptr); vkDestroyPipelineLayout(device, p->layout, nullptr); }
        m_GraphicsCache.clear(); m_RaytracingCache.clear(); m_ComputeCache.clear();
    }

    static VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code) {
        VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();
        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shader module!");
        }
        return shaderModule;
    }

    GraphicsPipeline& PipelineManager::GetGraphicsPipeline(const RenderPass& renderPass, const GraphicsPipelineDescription& description) {
        if (m_GraphicsCache.count(description.name)) return *m_GraphicsCache[description.name];

        auto pipeline = std::make_unique<GraphicsPipeline>();
        pipeline->description = description;

        // 1. Layout
        std::vector<std::string> shaderNames = { description.vertex_shader, description.fragment_shader };
        std::vector<VkDescriptorSetLayout> setLayouts = { 
            Application::Get().GetRenderState()->GetLayout(),
            m_ResourceManager.GetSceneDescriptorSetLayout()
        };
        if (renderPass.descriptor_set_layout != VK_NULL_HANDLE) setLayouts.push_back(renderPass.descriptor_set_layout);

        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = (uint32_t)setLayouts.size();
        layoutInfo.pSetLayouts = setLayouts.data();
        
        VkPushConstantRange pr{};
        pr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pr.offset = 0; pr.size = 128; 
        layoutInfo.pushConstantRangeCount = 1; layoutInfo.pPushConstantRanges = &pr;

        // 2. Shaders
        auto vertShader = ShaderManager::GetShader(description.vertex_shader);
        auto fragShader = ShaderManager::GetShader(description.fragment_shader);

        uint32_t pushConstantSize = std::max(vertShader->GetPushConstantSize(), fragShader->GetPushConstantSize());
        if (pushConstantSize > 0) {
            // Re-create layout if push constant size is different from initial assumption
            vkDestroyPipelineLayout(m_Context->GetDevice(), pipeline->layout, nullptr);
            
            VkPushConstantRange pr{};
            pr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pr.offset = 0; 
            pr.size = pushConstantSize;
            
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pr;
            
            if (vkCreatePipelineLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &pipeline->layout) != VK_SUCCESS) {
                throw std::runtime_error("failed to create pipeline layout with reflected push constants!");
            }
        }

        CH_CORE_INFO("PipelineManager: Creating Graphics Pipeline '{0}' (Push Constant Size: {1})", description.name, pushConstantSize);

        VkShaderModule vertModule = CreateShaderModule(m_Context->GetDevice(), vertShader->GetBytecode());
        VkShaderModule fragModule = CreateShaderModule(m_Context->GetDevice(), fragShader->GetBytecode());

        CH_CORE_INFO("PipelineManager: Created shader modules for '{0}'", description.name);

        VkPipelineShaderStageCreateInfo vertStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT; vertStage.module = vertModule; vertStage.pName = "main";
        VkPipelineShaderStageCreateInfo fragStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT; fragStage.module = fragModule; fragStage.pName = "main";
        VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

        // 3. States
        CH_CORE_INFO("PipelineManager: Setting up states for '{0}'", description.name);
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        
        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        if (description.vertex_shader.find("fullscreen") == std::string::npos) {
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
            vertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)attributeDescriptions.size();
            vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1; viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.polygonMode = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ? VK_POLYGON_MODE_FILL : VK_POLYGON_MODE_LINE;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE; // Simplified
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depthStencil.depthTestEnable = description.depth_test ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = description.depth_write ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
        const GraphicsPass& gp = std::get<GraphicsPass>(renderPass.pass);
        for (auto& att : gp.attachments) {
            if (VulkanUtils::IsDepthFormat(att.image.format)) continue;
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
        dynamicState.dynamicStateCount = 2; dynamicState.pDynamicStates = dynamicStates;

        // 4. Dynamic Rendering Info
        std::vector<VkFormat> colorFormats;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        for (auto& att : gp.attachments) {
            if (VulkanUtils::IsDepthFormat(att.image.format)) depthFormat = att.image.format;
            else colorFormats.push_back(att.image.format);
        }

        VkPipelineRenderingCreateInfo renderingCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        renderingCreateInfo.colorAttachmentCount = (uint32_t)colorFormats.size();
        renderingCreateInfo.pColorAttachmentFormats = colorFormats.data();
        renderingCreateInfo.depthAttachmentFormat = depthFormat;

        // 5. Create Pipeline
        VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineInfo.pNext = &renderingCreateInfo;
        pipelineInfo.stageCount = 2; pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipeline->layout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;

        if (vkCreateGraphicsPipelines(m_Context->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline->handle) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline!");
        }

        vkDestroyShaderModule(m_Context->GetDevice(), vertModule, nullptr);
        vkDestroyShaderModule(m_Context->GetDevice(), fragModule, nullptr);

        m_GraphicsCache[description.name] = std::move(pipeline);
        return *m_GraphicsCache[description.name];
    }

    RaytracingPipeline& PipelineManager::GetRaytracingPipeline(const RenderPass& renderPass, const RaytracingPipelineDescription& description) {
        std::string key = description.raygen_shader; 
        if (m_RaytracingCache.count(key)) return *m_RaytracingCache[key];

        auto pipeline = std::make_unique<RaytracingPipeline>();
        pipeline->description = description;

        std::vector<VkDescriptorSetLayout> setLayouts = { Application::Get().GetRenderState()->GetLayout() };
        if (renderPass.descriptor_set_layout != VK_NULL_HANDLE) setLayouts.push_back(renderPass.descriptor_set_layout);

        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = (uint32_t)setLayouts.size();
        layoutInfo.pSetLayouts = setLayouts.data();
        
        VkPushConstantRange pr{};
        pr.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        pr.offset = 0; pr.size = 128;
        layoutInfo.pushConstantRangeCount = 1; layoutInfo.pPushConstantRanges = &pr;

        vkCreatePipelineLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &pipeline->layout);

        // TODO: Implement actual RT pipeline creation if needed. 
        // For now, we return the layout-only object which still causes BindPipeline to fail if used.
        // But since user is seeing BindPipeline crash in Graphics pass first, let's fix that.

        m_RaytracingCache[key] = std::move(pipeline);
        return *m_RaytracingCache[key];
    }

    ComputePipeline& PipelineManager::GetComputePipeline(const RenderPass& renderPass, const ComputePipelineDescription::Kernel& kernel) {
        if (m_ComputeCache.count(kernel.name)) return *m_ComputeCache[kernel.name];

        auto pipeline = std::make_unique<ComputePipeline>();
        
        std::vector<VkDescriptorSetLayout> setLayouts = { Application::Get().GetRenderState()->GetLayout() };
        if (renderPass.descriptor_set_layout != VK_NULL_HANDLE) setLayouts.push_back(renderPass.descriptor_set_layout);

        VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layoutInfo.setLayoutCount = (uint32_t)setLayouts.size();
        layoutInfo.pSetLayouts = setLayouts.data();
        
        vkCreatePipelineLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &pipeline->layout);

        auto shader = ShaderManager::GetShader(kernel.shader);
        VkShaderModule module = CreateShaderModule(m_Context->GetDevice(), shader->GetBytecode());

        VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = pipeline->layout;

        vkCreateComputePipelines(m_Context->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline->handle);
        vkDestroyShaderModule(m_Context->GetDevice(), module, nullptr);

        m_ComputeCache[kernel.name] = std::move(pipeline);
        return *m_ComputeCache[kernel.name];
    }
}