#include "pch.h"
#include "PipelineManager.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/VulkanPipelinePresets.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Resources/Buffer.h"
#include "Core/EngineConfig.h"

namespace Chimera {

    static std::vector<uint32_t> LoadShaderCode(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("failed to open file: " + filename);
        size_t fileSize = (size_t)file.tellg();
        CH_CORE_INFO("PipelineManager: Loading shader '{0}' ({1} bytes)", filename, fileSize);
        std::vector<uint32_t> buffer(fileSize / 4);
        file.seekg(0);
        file.read((char*)buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    static VkPipelineShaderStageCreateInfo CreatePipelineShaderStage(VkDevice device, const std::string& filename, VkShaderStageFlagBits stage, std::unordered_map<std::string, std::filesystem::file_time_type>& timestamps) {
        std::string name = filename;
        if (name.find(".spv") == std::string::npos) name += ".spv";
        
        std::filesystem::path fullpath = std::filesystem::absolute(Config::SHADER_DIR + name);
        
        if (!std::filesystem::exists(fullpath)) {
            std::filesystem::path altPath = std::filesystem::absolute(Config::SHADER_DIR + std::filesystem::path(name).filename().string());
            if (std::filesystem::exists(altPath)) fullpath = altPath;
        }

        if (std::filesystem::exists(fullpath)) {
            timestamps[fullpath.string()] = std::filesystem::last_write_time(fullpath);
            CH_CORE_INFO("PipelineManager: Tracking shader '{0}'", fullpath.filename().string());
        }
        auto code = LoadShaderCode(fullpath.string());
        VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, code.size() * 4, code.data() };
        VkShaderModule module;
        vkCreateShaderModule(device, &createInfo, nullptr, &module);
        
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = stage;
        stageInfo.module = module;
        stageInfo.pName = "main";
        return stageInfo;
    }

    static inline uint32_t align_up(uint32_t value, uint32_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    PipelineManager::PipelineManager(std::shared_ptr<VulkanContext> context, ResourceManager& resourceManager)
        : m_Context(context), m_ResourceManager(resourceManager) {}

    PipelineManager::~PipelineManager() { ClearCache(); }

    void PipelineManager::ClearCache() {
        VkDevice device = m_Context->GetDevice();
        vkDeviceWaitIdle(device);
        for (auto& [name, p] : m_GraphicsCache) { vkDestroyPipeline(device, p->handle, nullptr); vkDestroyPipelineLayout(device, p->layout, nullptr); }
        for (auto& [name, p] : m_RaytracingCache) { vkDestroyPipeline(device, p->handle, nullptr); vkDestroyPipelineLayout(device, p->layout, nullptr); }
        for (auto& [name, p] : m_ComputeCache) { vkDestroyPipeline(device, p->handle, nullptr); vkDestroyPipelineLayout(device, p->layout, nullptr); }
        m_GraphicsCache.clear(); m_RaytracingCache.clear(); m_ComputeCache.clear();
    }

    bool PipelineManager::CheckForShaderUpdates() {
        bool changed = false;
        for (auto& [pathStr, lastTime] : m_ShaderTimestamps) {
            std::filesystem::path path(pathStr);
            if (std::filesystem::exists(path)) {
                auto currentTime = std::filesystem::last_write_time(path);
                if (currentTime != lastTime) { CH_CORE_INFO("Shader change detected: {0}", path.filename().string()); changed = true; }
            }
        }
        if (changed) ClearCache();
        return changed;
    }

    bool PipelineManager::CheckForSourceUpdates()
    {
        bool changed = false;
        std::filesystem::path sourceDir = Config::SHADER_SOURCE_DIR;
        if (!std::filesystem::exists(sourceDir)) return false;

        for (auto const& dir_entry : std::filesystem::recursive_directory_iterator(sourceDir))
        {
            if (dir_entry.is_regular_file()) {
                std::string ext = dir_entry.path().extension().string();
                if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".rgen" || ext == ".rchit" || ext == ".rmiss") {
                    std::string pathStr = dir_entry.path().string();
                    auto lastWriteTime = std::filesystem::last_write_time(dir_entry.path());

                    if (m_SourceTimestamps.find(pathStr) == m_SourceTimestamps.end()) {
                        m_SourceTimestamps[pathStr] = lastWriteTime;
                    } else if (m_SourceTimestamps[pathStr] != lastWriteTime) {
                        CH_CORE_INFO("Shader source change detected: {0}", dir_entry.path().filename().string());
                        m_SourceTimestamps[pathStr] = lastWriteTime;
                        changed = true;
                    }
                }
            }
        }
        return changed;
    }

    GraphicsPipeline& PipelineManager::GetGraphicsPipeline(const RenderPass& renderPass, const GraphicsPipelineDescription& description) {
        if (m_GraphicsCache.count(description.name)) return *m_GraphicsCache[description.name];
        CH_CORE_INFO("Creating Graphics Pipeline: {0}", description.name);
        const GraphicsPass &graphics_pass = std::get<GraphicsPass>(renderPass.pass);
        auto pipeline = std::make_unique<GraphicsPipeline>(); pipeline->description = description;
        std::vector<VkPipelineShaderStageCreateInfo> shader_stages = {
            CreatePipelineShaderStage(m_Context->GetDevice(), description.vertex_shader, VK_SHADER_STAGE_VERTEX_BIT, m_ShaderTimestamps),
            CreatePipelineShaderStage(m_Context->GetDevice(), description.fragment_shader, VK_SHADER_STAGE_FRAGMENT_BIT, m_ShaderTimestamps)
        };
        
        VkPipelineVertexInputStateCreateInfo vertex_input = VERTEX_INPUT_STATE_DEFAULT;
        if(description.vertex_input_state == VertexInputState::ImGui) vertex_input = VERTEX_INPUT_STATE_IMGUI;
        VkPipelineInputAssemblyStateCreateInfo input_assembly { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
        
        std::vector<VkPushConstantRange> push_ranges;
        if(description.push_constants.size > 0) {
            VkPushConstantRange range{};
            range.stageFlags = description.push_constants.shader_stage;
            range.offset = 0;
            range.size = description.push_constants.size;
            push_ranges.push_back(range);
        }

        std::vector<VkDescriptorSetLayout> layouts; layouts.push_back(m_ResourceManager.GetGlobalDescriptorSetLayout());
        if(renderPass.descriptor_set_layout != VK_NULL_HANDLE) layouts.push_back(renderPass.descriptor_set_layout);
        
        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = (uint32_t)layouts.size();
        layout_info.pSetLayouts = layouts.data();
        layout_info.pushConstantRangeCount = (uint32_t)push_ranges.size();
        layout_info.pPushConstantRanges = push_ranges.data();
        vkCreatePipelineLayout(m_Context->GetDevice(), &layout_info, nullptr, &pipeline->layout);
        
        VkGraphicsPipelineCreateInfo pipeline_info { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeline_info.stageCount = 2; pipeline_info.pStages = shader_stages.data(); pipeline_info.pVertexInputState = &vertex_input; pipeline_info.pInputAssemblyState = &input_assembly; pipeline_info.layout = pipeline->layout; pipeline_info.renderPass = graphics_pass.handle;
        if (description.rasterization_state == RasterizationState::CullClockwise) pipeline_info.pRasterizationState = &RASTERIZATION_STATE_CULL_CLOCKWISE;
        else if (description.rasterization_state == RasterizationState::CullCounterClockwise) pipeline_info.pRasterizationState = &RASTERIZATION_STATE_CULL_COUNTER_CLOCKWISE;
        else pipeline_info.pRasterizationState = &RASTERIZATION_STATE_CULL_NONE;
        VkPipelineMultisampleStateCreateInfo multisample = MULTISAMPLE_STATE_OFF; pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pDepthStencilState = (description.depth_stencil_state == DepthStencilState::On) ? &DEPTH_STENCIL_STATE_ON : &DEPTH_STENCIL_STATE_OFF;
        if (description.dynamic_state == DynamicState::ViewportScissor) pipeline_info.pDynamicState = &DYNAMIC_STATE_VIEWPORT_SCISSOR;
        else if (description.dynamic_state == DynamicState::DepthBias) pipeline_info.pDynamicState = &DYNAMIC_STATE_DEPTH_BIAS;
        std::vector<VkPipelineColorBlendAttachmentState> blend_atts;
        for(auto& att : graphics_pass.attachments) if(!VulkanUtils::IsDepthFormat(att.image.format)) blend_atts.push_back(COLOR_BLEND_ATTACHMENT_STATE_OFF);
        VkPipelineColorBlendStateCreateInfo color_blend { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, (uint32_t)blend_atts.size(), blend_atts.data() };
        pipeline_info.pColorBlendState = &color_blend;
        VkViewport vp{}; VkRect2D sc{}; VkPipelineViewportStateCreateInfo vp_state { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, &vp, 1, &sc };
        pipeline_info.pViewportState = &vp_state;
        
        VkPipelineRenderingCreateInfo rendering_create_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        std::vector<VkFormat> color_formats; VkFormat depth_format = VK_FORMAT_UNDEFINED;
        if (graphics_pass.handle == VK_NULL_HANDLE) {
            for (const auto& att : graphics_pass.attachments) { if (VulkanUtils::IsDepthFormat(att.image.format)) depth_format = att.image.format; else color_formats.push_back(att.image.format); }
            rendering_create_info.colorAttachmentCount = (uint32_t)color_formats.size(); rendering_create_info.pColorAttachmentFormats = color_formats.data(); rendering_create_info.depthAttachmentFormat = depth_format;
            pipeline_info.pNext = &rendering_create_info;
        }
        vkCreateGraphicsPipelines(m_Context->GetDevice(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->handle);
        for(auto& stage : shader_stages) vkDestroyShaderModule(m_Context->GetDevice(), stage.module, nullptr);
        m_GraphicsCache[description.name] = std::move(pipeline);
        return *m_GraphicsCache[description.name];
    }

    RaytracingPipeline& PipelineManager::GetRaytracingPipeline(const RenderPass& renderPass, const RaytracingPipelineDescription& description) {
        if (m_RaytracingCache.count(description.name)) return *m_RaytracingCache[description.name];
        CH_CORE_INFO("Creating Raytracing Pipeline: {0}", description.name);
        auto device = m_Context->GetDevice(); auto pipeline = std::make_unique<RaytracingPipeline>(); pipeline->description = description;
        std::vector<VkPipelineShaderStageCreateInfo> stages; std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
        stages.push_back(CreatePipelineShaderStage(device, description.raygen_shader, VK_SHADER_STAGE_RAYGEN_BIT_KHR, m_ShaderTimestamps));
        groups.push_back({ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, nullptr, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, 0, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR });
        for (const auto& s : description.miss_shaders) {
            stages.push_back(CreatePipelineShaderStage(device, s, VK_SHADER_STAGE_MISS_BIT_KHR, m_ShaderTimestamps));
            groups.push_back({ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, nullptr, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, (uint32_t)stages.size()-1, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR });
        }
        for (const auto& h : description.hit_shaders) {
            VkRayTracingShaderGroupCreateInfoKHR g { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, nullptr, VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR };
            if (!h.closest_hit.empty()) { stages.push_back(CreatePipelineShaderStage(device, h.closest_hit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, m_ShaderTimestamps)); g.closestHitShader = (uint32_t)stages.size() - 1; }
            groups.push_back(g);
        }
        
        std::vector<VkDescriptorSetLayout> layouts; layouts.push_back(m_ResourceManager.GetGlobalDescriptorSetLayout());
        if (renderPass.descriptor_set_layout != VK_NULL_HANDLE) layouts.push_back(renderPass.descriptor_set_layout);
        
        std::vector<VkPushConstantRange> push_ranges;
        if(description.push_constants.size > 0) {
            VkPushConstantRange range{};
            range.stageFlags = description.push_constants.shader_stage;
            range.offset = 0;
            range.size = description.push_constants.size;
            push_ranges.push_back(range);
        }

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = (uint32_t)layouts.size();
        layout_info.pSetLayouts = layouts.data();
        layout_info.pushConstantRangeCount = (uint32_t)push_ranges.size();
        layout_info.pPushConstantRanges = push_ranges.data();
        vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline->layout);
        
        VkRayTracingPipelineCreateInfoKHR pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipeline_info.stageCount = (uint32_t)stages.size();
        pipeline_info.pStages = stages.data();
        pipeline_info.groupCount = (uint32_t)groups.size();
        pipeline_info.pGroups = groups.data();
        pipeline_info.maxPipelineRayRecursionDepth = 1;
        pipeline_info.layout = pipeline->layout;
        vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->handle);
        for (auto& s : stages) vkDestroyShaderModule(device, s.module, nullptr);
        
        auto rtProps = m_Context->GetRayTracingProperties(); uint32_t h_size = rtProps.shaderGroupHandleSize; uint32_t h_aligned = align_up(h_size, rtProps.shaderGroupBaseAlignment);
        uint32_t group_count = (uint32_t)groups.size(); std::vector<uint8_t> handles(group_count * h_size);
        vkGetRayTracingShaderGroupHandlesKHR(device, pipeline->handle, 0, group_count, (uint32_t)handles.size(), handles.data());
        pipeline->sbt_buffer = std::make_shared<Buffer>(m_Context->GetAllocator(), group_count * h_aligned, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        uint8_t* sbt_ptr = (uint8_t*)pipeline->sbt_buffer->Map();
        for (uint32_t i = 0; i < group_count; i++) memcpy(sbt_ptr + i * h_aligned, handles.data() + i * h_size, h_size);
        pipeline->sbt_buffer->Unmap();
        VkDeviceAddress sbt_addr = pipeline->sbt_buffer->GetDeviceAddress();
        pipeline->raygen_sbt.strided_device_address_region = { sbt_addr, h_aligned, h_aligned };
        pipeline->miss_sbt.strided_device_address_region = { sbt_addr + h_aligned, h_aligned, (uint32_t)description.miss_shaders.size() * h_aligned };
        pipeline->hit_sbt.strided_device_address_region = { sbt_addr + (1 + (uint32_t)description.miss_shaders.size()) * h_aligned, h_aligned, (uint32_t)description.hit_shaders.size() * h_aligned };
        m_RaytracingCache[description.name] = std::move(pipeline);
        return *m_RaytracingCache[description.name];
    }

    ComputePipeline& PipelineManager::GetComputePipeline(const RenderPass& renderPass, const PushConstantDescription& pushConstants, const ComputeKernel& kernel) {
        if (m_ComputeCache.count(kernel.shader)) return *m_ComputeCache[kernel.shader];
        CH_CORE_INFO("Creating Compute Pipeline: {0}", kernel.shader);
        auto pipeline = std::make_unique<ComputePipeline>(); pipeline->push_constant_description = pushConstants;
        auto shader_stage = CreatePipelineShaderStage(m_Context->GetDevice(), kernel.shader, VK_SHADER_STAGE_COMPUTE_BIT, m_ShaderTimestamps);
        std::vector<VkDescriptorSetLayout> layouts; layouts.push_back(m_ResourceManager.GetGlobalDescriptorSetLayout());
        if (renderPass.descriptor_set_layout != VK_NULL_HANDLE) layouts.push_back(renderPass.descriptor_set_layout);
        
        std::vector<VkPushConstantRange> push_ranges;
        if(pushConstants.size > 0) {
            VkPushConstantRange range{};
            range.stageFlags = pushConstants.shader_stage;
            range.offset = 0;
            range.size = pushConstants.size;
            push_ranges.push_back(range);
        }

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = (uint32_t)layouts.size();
        layout_info.pSetLayouts = layouts.data();
        layout_info.pushConstantRangeCount = (uint32_t)push_ranges.size();
        layout_info.pPushConstantRanges = push_ranges.data();
        vkCreatePipelineLayout(m_Context->GetDevice(), &layout_info, nullptr, &pipeline->layout);
        
        VkComputePipelineCreateInfo pipeline_info { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, shader_stage, pipeline->layout };
        vkCreateComputePipelines(m_Context->GetDevice(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline->handle);
        vkDestroyShaderModule(m_Context->GetDevice(), shader_stage.module, nullptr);
        m_ComputeCache[kernel.shader] = std::move(pipeline);
        return *m_ComputeCache[kernel.shader];
    }
}
