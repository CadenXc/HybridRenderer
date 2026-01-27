#include "pch.h"
#include "gfx/pipeline/Pipeline.h"
#include "gfx/resources/ResourceManager.h"
#include "gfx/vulkan/VulkanContext.h"
#include "gfx/pipeline/VulkanPipelinePresets.h"
#include "gfx/utils/VulkanBarrier.h"
#include "gfx/utils/VulkanShaderUtils.h"
#include "gfx/utils/VulkanDescriptorUtils.h"
#include "gfx/resources/Buffer.h"
#include "core/Config.h"
#include "core/utilities/FileIO.h"

namespace Chimera {
namespace VulkanUtils {

    // Helper to align sizes
    inline uint32_t align_up(uint32_t value, uint32_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    // Helper to load shader code
    static std::vector<uint32_t> LoadShaderCode(const std::string& filename) {
        // Implement simple read file to uint32_t vector
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + filename);
        }
        size_t fileSize = (size_t)file.tellg();
        std::vector<uint32_t> buffer(fileSize / 4);
        file.seekg(0);
        file.read((char*)buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    static VkPipelineShaderStageCreateInfo CreatePipelineShaderStage(VkDevice device, const char* filename, VkShaderStageFlagBits stage) {
        std::string name(filename);
        // Basic check to see if extension is needed (simplistic)
        if (name.find(".spv") == std::string::npos) {
            name += ".spv";
        }
        
        std::string fullpath = std::string(Config::SHADER_DIR) + name;

        auto code = LoadShaderCode(fullpath);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size() * 4;
        createInfo.pCode = code.data();

        VkShaderModule module;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
             throw std::runtime_error("failed to create shader module: " + fullpath);
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = stage;
        stageInfo.module = module;
        stageInfo.pName = "main";
        return stageInfo;
    }

    GraphicsPipeline CreateGraphicsPipeline(std::shared_ptr<VulkanContext> context, ResourceManager &resource_manager, 
        RenderPass &render_pass, GraphicsPipelineDescription description) {
        
        // Assert graphics pass
        // if (!std::holds_alternative<GraphicsPass>(render_pass.pass)) assert(false);
        GraphicsPass &graphics_pass = std::get<GraphicsPass>(render_pass.pass);
        GraphicsPipeline pipeline {
            .description = description
        };

        std::array<VkPipelineShaderStageCreateInfo, 2> shader_stage_infos {
            CreatePipelineShaderStage(context->GetDevice(), description.vertex_shader, VK_SHADER_STAGE_VERTEX_BIT),
            CreatePipelineShaderStage(context->GetDevice(), description.fragment_shader, VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        // Specialization Constants... (Simplified for now, copy logic if needed)
        // ... (Skipping specialization for brevity, assume none for now or implement later)

        VkPipelineVertexInputStateCreateInfo vertex_input_state_info = VERTEX_INPUT_STATE_DEFAULT;
        if(description.vertex_input_state == VertexInputState::ImGui) {
            vertex_input_state_info = VERTEX_INPUT_STATE_IMGUI;
        }

        VkPipelineInputAssemblyStateCreateInfo input_assembly_state_info {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE
        };

        std::vector<VkPushConstantRange> push_constants;
        if(description.push_constants.size > 0) {
            push_constants.emplace_back(
                VkPushConstantRange {
                    .stageFlags = description.push_constants.shader_stage,
                    .offset = 0,
                    .size = description.push_constants.size
                }
            );
        }

        std::vector<VkDescriptorSetLayout> descriptor_set_layouts { 
            resource_manager.GetGlobalDescriptorSetLayout0(),
            resource_manager.GetGlobalDescriptorSetLayout1(),
            resource_manager.GetPerFrameDescriptorSetLayout(),
        };
        if(render_pass.descriptor_set_layout != VK_NULL_HANDLE) {
            descriptor_set_layouts.emplace_back(render_pass.descriptor_set_layout);
        }

        VkPipelineLayoutCreateInfo layout_info {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(descriptor_set_layouts.size()),
            .pSetLayouts = descriptor_set_layouts.data(),
            .pushConstantRangeCount = static_cast<uint32_t>(push_constants.size()),
            .pPushConstantRanges = push_constants.empty() ? nullptr : push_constants.data()
        };

        if (vkCreatePipelineLayout(context->GetDevice(), &layout_info, nullptr, &pipeline.layout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        VkGraphicsPipelineCreateInfo pipeline_info {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = static_cast<uint32_t>(shader_stage_infos.size()),
            .pStages = shader_stage_infos.data(),
            .pVertexInputState = &vertex_input_state_info,
            .pInputAssemblyState = &input_assembly_state_info,
            .layout = pipeline.layout,
            .renderPass = graphics_pass.handle,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE
        };

        VkPipelineRasterizationStateCreateInfo rasterization_state = RASTERIZATION_STATE_DEFAULT;
        pipeline_info.pRasterizationState = &rasterization_state;

        VkPipelineMultisampleStateCreateInfo multisample_state = MULTISAMPLE_STATE_OFF;
        pipeline_info.pMultisampleState = &multisample_state;
        
        switch(description.depth_stencil_state) {
        case DepthStencilState::On:
            pipeline_info.pDepthStencilState = &DEPTH_STENCIL_STATE_ON; break;
        case DepthStencilState::Off:
            pipeline_info.pDepthStencilState = &DEPTH_STENCIL_STATE_OFF; break;
        }

        switch(description.dynamic_state) {
        case DynamicState::ViewportScissor:
            pipeline_info.pDynamicState = &DYNAMIC_STATE_VIEWPORT_SCISSOR; break;
        case DynamicState::DepthBias:
            pipeline_info.pDynamicState = &DYNAMIC_STATE_DEPTH_BIAS; break;
        case DynamicState::None: break;
        }

        bool contains_render_output = false;
        if (!graphics_pass.attachments.empty()) {
             for(auto& att : graphics_pass.attachments) {
                 if(att.name && strcmp(att.name, "RENDER_OUTPUT") == 0) contains_render_output = true;
             }
        }
        
        // Setup Color Blend
        std::vector<VkPipelineColorBlendAttachmentState> color_blend_states;
        for(TransientResource &attachment : graphics_pass.attachments) {
            if(VulkanUtils::IsDepthFormat(attachment.image.format)) {
                continue;
            }
            color_blend_states.emplace_back(COLOR_BLEND_ATTACHMENT_STATE_OFF);
        }
        VkPipelineColorBlendStateCreateInfo color_blend_state {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = static_cast<uint32_t>(color_blend_states.size()),
            .pAttachments = color_blend_states.data()
        };
        pipeline_info.pColorBlendState = &color_blend_state;
        
        VkViewport viewport{}; // Placeholder, dynamic state handles it usually
        VkRect2D scissor{};
        VkPipelineViewportStateCreateInfo viewport_state_info {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .pViewports = &viewport,
            .scissorCount = 1,
            .pScissors = &scissor
        };
        pipeline_info.pViewportState = &viewport_state_info;

        // Detect Dynamic Rendering if RenderPass handle is null
        VkPipelineRenderingCreateInfoKHR rendering_create_info{};
        std::vector<VkFormat> color_formats;
        VkFormat depth_format = VK_FORMAT_UNDEFINED;

        if (graphics_pass.handle == VK_NULL_HANDLE) {
            rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            
            for (const auto& att : graphics_pass.attachments) {
                if (att.type != TransientResourceType::Image) continue;
                
                if (VulkanUtils::IsDepthFormat(att.image.format)) {
                    depth_format = att.image.format;
                } else {
                    color_formats.push_back(att.image.format);
                }
            }
            
            rendering_create_info.colorAttachmentCount = static_cast<uint32_t>(color_formats.size());
            rendering_create_info.pColorAttachmentFormats = color_formats.data();
            rendering_create_info.depthAttachmentFormat = depth_format;
            // Stencil format if needed
            if (depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT || depth_format == VK_FORMAT_D24_UNORM_S8_UINT) {
                rendering_create_info.stencilAttachmentFormat = depth_format;
            }

            pipeline_info.pNext = &rendering_create_info;
        }

        if (vkCreateGraphicsPipelines(context->GetDevice(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline.handle) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline!");
        }

        // Cleanup shader modules
        for(auto& stage : shader_stage_infos) {
            vkDestroyShaderModule(context->GetDevice(), stage.module, nullptr);
        }

        return pipeline;
    }

    RaytracingPipeline CreateRaytracingPipeline(std::shared_ptr<VulkanContext> context, ResourceManager &resource_manager,
        RenderPass &render_pass, RaytracingPipelineDescription description, 
        const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &raytracing_properties) {
        
        RaytracingPipeline pipeline {
            .description = description
        };

        auto device = context->GetDevice();
        std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

        // 1. RayGen Shader
        shader_stages.push_back(CreatePipelineShaderStage(device, description.raygen_shader, VK_SHADER_STAGE_RAYGEN_BIT_KHR));
        VkRayTracingShaderGroupCreateInfoKHR raygen_group { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
        raygen_group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        raygen_group.generalShader = static_cast<uint32_t>(shader_stages.size() - 1);
        raygen_group.closestHitShader = VK_SHADER_UNUSED_KHR;
        raygen_group.anyHitShader = VK_SHADER_UNUSED_KHR;
        raygen_group.intersectionShader = VK_SHADER_UNUSED_KHR;
        shader_groups.push_back(raygen_group);

        // 2. Miss Shaders
        for (const auto& shader : description.miss_shaders) {
            shader_stages.push_back(CreatePipelineShaderStage(device, shader, VK_SHADER_STAGE_MISS_BIT_KHR));
            VkRayTracingShaderGroupCreateInfoKHR miss_group { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
            miss_group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            miss_group.generalShader = static_cast<uint32_t>(shader_stages.size() - 1);
            miss_group.closestHitShader = VK_SHADER_UNUSED_KHR;
            miss_group.anyHitShader = VK_SHADER_UNUSED_KHR;
            miss_group.intersectionShader = VK_SHADER_UNUSED_KHR;
            shader_groups.push_back(miss_group);
        }

        // 3. Hit Shaders
        for (const auto& hit_shader : description.hit_shaders) {
            VkRayTracingShaderGroupCreateInfoKHR hit_group { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
            hit_group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            hit_group.generalShader = VK_SHADER_UNUSED_KHR;
            hit_group.anyHitShader = VK_SHADER_UNUSED_KHR;
            hit_group.intersectionShader = VK_SHADER_UNUSED_KHR;

            if (hit_shader.closest_hit) {
                shader_stages.push_back(CreatePipelineShaderStage(device, hit_shader.closest_hit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
                hit_group.closestHitShader = static_cast<uint32_t>(shader_stages.size() - 1);
            }
            if (hit_shader.any_hit) {
                shader_stages.push_back(CreatePipelineShaderStage(device, hit_shader.any_hit, VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
                hit_group.anyHitShader = static_cast<uint32_t>(shader_stages.size() - 1);
            }
            shader_groups.push_back(hit_group);
        }

        // 4. Pipeline Layout
        std::vector<VkDescriptorSetLayout> layouts;
        if (render_pass.descriptor_set_layout != VK_NULL_HANDLE) {
            layouts.push_back(render_pass.descriptor_set_layout);
        }
        layouts.push_back(resource_manager.GetGlobalDescriptorSetLayout0());
        layouts.push_back(resource_manager.GetGlobalDescriptorSetLayout1());
        layouts.push_back(resource_manager.GetPerFrameDescriptorSetLayout());

        VkPipelineLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layout_info.setLayoutCount = static_cast<uint32_t>(layouts.size());
        layout_info.pSetLayouts = layouts.data();
        // Assuming push constants for RT as well if needed
        
        if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline.layout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create RT pipeline layout!");
        }

        // 5. Create Pipeline
        VkRayTracingPipelineCreateInfoKHR pipeline_info { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
        pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
        pipeline_info.pStages = shader_stages.data();
        pipeline_info.groupCount = static_cast<uint32_t>(shader_groups.size());
        pipeline_info.pGroups = shader_groups.data();
        pipeline_info.maxPipelineRayRecursionDepth = 1; // Default
        pipeline_info.layout = pipeline.layout;

        if (vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline.handle) != VK_SUCCESS) {
            throw std::runtime_error("failed to create raytracing pipeline!");
        }

        // 6. Cleanup shader modules
        for (auto& stage : shader_stages) {
            vkDestroyShaderModule(device, stage.module, nullptr);
        }

        // 7. Shader Binding Table
        uint32_t handle_size = raytracing_properties.shaderGroupHandleSize;
        uint32_t handle_alignment = raytracing_properties.shaderGroupBaseAlignment;
        uint32_t handle_size_aligned = align_up(handle_size, handle_alignment);
        uint32_t group_count = static_cast<uint32_t>(shader_groups.size());
        uint32_t sbt_size = group_count * handle_size_aligned;

        std::vector<uint8_t> shader_handle_storage(group_count * handle_size);
        if (vkGetRayTracingShaderGroupHandlesKHR(device, pipeline.handle, 0, group_count, static_cast<uint32_t>(shader_handle_storage.size()), shader_handle_storage.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to get raytracing shader group handles!");
        }

        // For simplicity in this generic implementation, we'll create one buffer for the whole SBT
        // and partition it for raygen, miss, and hit.
        // Note: Real implementations might need separate buffers or more complex alignment.
        
        auto sbt_buffer = std::make_shared<Buffer>(
            context->GetAllocator(), 
            sbt_size, 
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        uint8_t* sbt_ptr = (uint8_t*)sbt_buffer->Map();
        for (uint32_t i = 0; i < group_count; i++) {
            memcpy(sbt_ptr + i * handle_size_aligned, shader_handle_storage.data() + i * handle_size, handle_size);
        }
        sbt_buffer->Unmap();

        VkDeviceAddress sbt_address = sbt_buffer->GetDeviceAddress();

        // Assign regions. RayGen is always first group.
        pipeline.raygen_sbt.strided_device_address_region = { sbt_address, handle_size_aligned, handle_size_aligned };
        
        // Miss shaders follow RayGen
        uint32_t miss_offset = 1 * handle_size_aligned;
        pipeline.miss_sbt.strided_device_address_region = { sbt_address + miss_offset, handle_size_aligned, static_cast<uint32_t>(description.miss_shaders.size()) * handle_size_aligned };
        
        // Hit shaders follow Miss
        uint32_t hit_offset = miss_offset + static_cast<uint32_t>(description.miss_shaders.size()) * handle_size_aligned;
        pipeline.hit_sbt.strided_device_address_region = { sbt_address + hit_offset, handle_size_aligned, static_cast<uint32_t>(description.hit_shaders.size()) * handle_size_aligned };

        pipeline.sbt_buffer = sbt_buffer;

        return pipeline;
    }

    ComputePipeline CreateComputePipeline(std::shared_ptr<VulkanContext> context, ResourceManager &resource_manager,
        RenderPass &render_pass, PushConstantDescription push_constant_description, ComputeKernel kernel) {
        
        ComputePipeline pipeline {
            .push_constant_description = push_constant_description
        };

        auto device = context->GetDevice();
        auto shader_stage = CreatePipelineShaderStage(device, kernel.shader, VK_SHADER_STAGE_COMPUTE_BIT);

        // Layout: Set 0 = Pass Specific, Set 1 = Global
        std::vector<VkDescriptorSetLayout> layouts;
        if (render_pass.descriptor_set_layout != VK_NULL_HANDLE) {
            layouts.push_back(render_pass.descriptor_set_layout);
        }
        layouts.push_back(resource_manager.GetGlobalDescriptorSetLayout0());

        std::vector<VkPushConstantRange> push_constants;
        if (push_constant_description.size > 0) {
            push_constants.push_back({
                push_constant_description.shader_stage,
                0,
                push_constant_description.size
            });
        }

        VkPipelineLayoutCreateInfo layout_info { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layout_info.setLayoutCount = static_cast<uint32_t>(layouts.size());
        layout_info.pSetLayouts = layouts.data();
        layout_info.pushConstantRangeCount = static_cast<uint32_t>(push_constants.size());
        layout_info.pPushConstantRanges = push_constants.data();

        if (vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline.layout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create compute pipeline layout!");
        }

        VkComputePipelineCreateInfo pipeline_info { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        pipeline_info.stage = shader_stage;
        pipeline_info.layout = pipeline.layout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline.handle) != VK_SUCCESS) {
            throw std::runtime_error("failed to create compute pipeline!");
        }

        vkDestroyShaderModule(device, shader_stage.module, nullptr);

        return pipeline;
    }

}
}



