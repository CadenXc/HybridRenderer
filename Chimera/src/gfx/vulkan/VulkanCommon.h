#pragma once
#include "pch.h"
#include <variant>
#include <vector>
#include <array>
#include <string>
#include <functional>
#include <memory>

namespace Chimera {

    // Forward declarations
    class Buffer;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    struct ImageDescription 
    {
        uint32_t width;
        uint32_t height;
        VkFormat format;
        VkImageUsageFlags usage;
        VkSampleCountFlagBits samples;

        bool operator==(const ImageDescription& other) const 
        {
            return width == other.width && height == other.height && 
                   format == other.format && usage == other.usage && 
                   samples == other.samples;
        }
    };

    struct BufferDescription 
    {
        VkDeviceSize size;
        VkBufferUsageFlags usage;
        VmaMemoryUsage memory_usage;

        bool operator==(const BufferDescription& other) const 
        {
            return size == other.size && usage == other.usage && memory_usage == other.memory_usage;
        }
    };

    struct GraphImage 
    {
        VkImage handle;
        VkImageView view;
        VmaAllocation allocation;
        uint32_t width;
        uint32_t height;
        VkFormat format;
        VkImageUsageFlags usage;
        bool is_external = false;
    };

    enum class VertexInputState { Default, Empty, ImGui };
    enum class RasterizationState { CullClockwise, CullCounterClockwise, CullNone };
    enum class MultisampleState { Off, On };
    enum class DepthStencilState { Off, On };
    enum class ColorBlendState { Off, ImGui };
    enum class DynamicState { None, Viewport, ViewportScissor, DepthBias };

    struct PushConstantDescription {
        uint32_t size;
        VkShaderStageFlags shader_stage;
    };
    static const PushConstantDescription PUSHCONSTANTS_NONE = { 0, 0 };

    struct SpecializationConstantsDescription {
        VkShaderStageFlags shader_stage;
        std::vector<int> specialization_constants;
    };

    enum class TransientResourceType { Image, Buffer };
    enum class TransientImageType { AttachmentImage, SampledImage, StorageImage };

    struct TransientImage {
        TransientImageType type;
        uint32_t width;
        uint32_t height;
        VkFormat format;
        uint32_t binding;
        VkClearValue clear_value;
        bool multisampled;
    };

    struct TransientBuffer {
        uint32_t stride;
        uint32_t count;
    };

    struct TransientResource {
        TransientResourceType type;
        const char *name;
        union {
            TransientImage image;
            TransientBuffer buffer;
        };
    };

    struct GraphicsPipelineDescription {
        const char *name;
        const char *vertex_shader;
        const char *fragment_shader;
        VertexInputState vertex_input_state;
        MultisampleState multisample_state;
        DepthStencilState depth_stencil_state;
        DynamicState dynamic_state;
        PushConstantDescription push_constants;
        SpecializationConstantsDescription specialization_constants_description;
    };

    struct GraphicsPipeline {
        GraphicsPipelineDescription description;
        VkPipeline handle;
        VkPipelineLayout layout;
    };

    struct HitShader {
        const char *closest_hit;
        const char *any_hit;
    };

    struct RaytracingPipelineDescription {
        const char *name;
        const char *raygen_shader;
        std::vector<const char *> miss_shaders;
        std::vector<HitShader> hit_shaders;
    };
    
    struct ShaderBindingTable {
        VkStridedDeviceAddressRegionKHR strided_device_address_region;
    };

    struct RaytracingPipeline {
        RaytracingPipelineDescription description;
        uint32_t shader_group_size;
        ShaderBindingTable raygen_sbt;
        ShaderBindingTable miss_sbt;
        ShaderBindingTable hit_sbt;
        ShaderBindingTable call_sbt;
        std::shared_ptr<class Buffer> sbt_buffer;
        VkPipeline handle;
        VkPipelineLayout layout;
    };

    struct ComputeKernel {
        const char *shader;
    };

    struct ComputePipelineDescription {
        std::vector<ComputeKernel> kernels;
        PushConstantDescription push_constant_description;
    };

    struct ComputePipeline {
        VkPipeline handle;
        VkPipelineLayout layout;
        PushConstantDescription push_constant_description;
    };

    class GraphicsExecutionContext;
    using GraphicsExecutionCallback = std::function<void(GraphicsExecutionContext &)>;
    using ExecuteGraphicsCallback = std::function<void(std::string, GraphicsExecutionCallback)>;
    using GraphicsPassCallback = std::function<void(ExecuteGraphicsCallback)>;

    class RaytracingExecutionContext;
    using RaytracingExecutionCallback = std::function<void(RaytracingExecutionContext &)>;
    using ExecuteRaytracingCallback = std::function<void(std::string, RaytracingExecutionCallback)>;
    using RaytracingPassCallback = std::function<void(ExecuteRaytracingCallback)>;

    class ComputeExecutionContext;
    using ComputePassCallback = std::function<void(ComputeExecutionContext &)>;

    struct GraphicsPass {
        VkRenderPass handle;
        std::vector<TransientResource> attachments;
        std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> framebuffers;
        GraphicsPassCallback callback;
    };

    struct ImageAccess {
        VkImageLayout layout;
        VkAccessFlags access_flags;
        VkPipelineStageFlags stage_flags;
    };

    struct RaytracingPass { RaytracingPassCallback callback; };
    struct ComputePass { ComputePassCallback callback; };

    struct RenderPass {
        const char *name;
        VkDescriptorSetLayout descriptor_set_layout;
        VkDescriptorSet descriptor_set;
        std::variant<GraphicsPass, RaytracingPass, ComputePass> pass;
    };

    struct ComputePassDescription {
        ComputePipelineDescription pipeline_description;
        ComputePassCallback callback;
    };

    struct GraphicsPassDescription {
        std::vector<GraphicsPipelineDescription> pipeline_descriptions;
        GraphicsPassCallback callback;
    };

    struct RaytracingPassDescription {
        RaytracingPipelineDescription pipeline_description;
        RaytracingPassCallback callback;
    };

    struct RenderPassDescription {
        const char *name;
        std::vector<TransientResource> dependencies;
        std::vector<TransientResource> outputs;
        std::variant<GraphicsPassDescription, RaytracingPassDescription, ComputePassDescription> description;
    };

}
