#pragma once
#include "Renderer/ChimeraCommon.h"
#include "GraphicsExecutionContext.h"
#include "RaytracingExecutionContext.h"
#include "ComputeExecutionContext.h"
#include <variant>
#include <vector>
#include <deque>
#include <string>
#include <functional>

namespace Chimera
{
    // 1. 资源描述
    struct ImageDescription
    {
        VkFormat format;
    };

    struct TransientResource
    {
        std::string name;
        TransientResourceType type;
        
        struct ImageInfo
        {
            VkFormat format;
            TransientImageType type;
            uint32_t binding = 0xFFFFFFFF;
            VkClearValue clear_value = { {0,0,0,1} };
        } image;

        struct BufferInfo
        {
            uint32_t count = 1;
            uint32_t binding = 0xFFFFFFFF;
            VkBuffer handle = VK_NULL_HANDLE;
        } buffer;

        struct ASInfo
        {
            uint32_t binding = 0xFFFFFFFF;
            VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        } as;

        static TransientResource Attachment(const std::string& n, VkFormat f, VkClearValue c = {{0,0,0,1}})
        {
            TransientResource r;
            r.name = n;
            r.type = TransientResourceType::Image;
            r.image = {f, TransientImageType::AttachmentImage, 0xFFFFFFFF, c};
            return r;
        }

        static TransientResource Image(const std::string& n, VkFormat f)
        {
            TransientResource r;
            r.name = n;
            r.type = TransientResourceType::Image;
            r.image.format = f;
            r.image.type = TransientImageType::SampledImage;
            r.image.binding = 0xFFFFFFFF;
            return r;
        }

        static TransientResource StorageImage(const std::string& n, VkFormat f)
        {
            TransientResource r;
            r.name = n;
            r.type = TransientResourceType::Image;
            r.image.format = f;
            r.image.type = TransientImageType::StorageImage;
            r.image.binding = 0xFFFFFFFF;
            return r;
        }

        static TransientResource Buffer(const std::string& n, uint32_t c = 1)
        {
            TransientResource r;
            r.name = n;
            r.type = TransientResourceType::Buffer;
            r.buffer.count = c;
            return r;
        }

        static TransientResource Sampler(const std::string& n, uint32_t c = 1)
        {
            TransientResource r;
            r.name = n;
            r.type = TransientResourceType::Sampler;
            r.buffer.count = c;
            return r;
        }

        static TransientResource AccelerationStructure(const std::string& n)
        {
            TransientResource r;
            r.name = n;
            r.type = TransientResourceType::AccelerationStructure;
            return r;
        }
    };

    struct GraphImage
    { 
        VkImage handle = VK_NULL_HANDLE; 
        VkImageView view = VK_NULL_HANDLE; 
        VkImageView debug_view = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr; 
        uint32_t width = 0; 
        uint32_t height = 0; 
        VkFormat format = VK_FORMAT_UNDEFINED; 
        VkImageUsageFlags usage = 0; 
        bool is_external = false; 
    };

    struct ImageAccess
    {
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkAccessFlags access_flags = 0;
        VkPipelineStageFlags stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    };

    // 2. 回调定义
    typedef std::function<void(GraphicsExecutionContext&)> GraphicsExecutionCallback;
    typedef std::function<void(std::string, GraphicsExecutionCallback)> ExecuteGraphicsCallback;
    typedef std::function<void(ExecuteGraphicsCallback&)> GraphicsPassCallback;
    
    typedef std::function<void(RaytracingExecutionContext&)> RaytracingExecutionCallback;
    typedef std::function<void(std::string, RaytracingExecutionCallback)> ExecuteRaytracingCallback;
    typedef std::function<void(ExecuteRaytracingCallback&)> RaytracingPassCallback;
    
    typedef std::function<void(ComputeExecutionContext&)> ComputePassCallback;

    // 3. 管线描述
    struct GraphicsPipelineDescription
    {
        std::string name;
        std::string vertex_shader;
        std::string fragment_shader;
        bool depth_test = true;
        bool depth_write = true;
        VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    };

    struct RaytracingPipelineDescription
    {
        std::string raygen_shader;
        std::vector<std::string> miss_shaders;
        
        struct HitGroup
        {
            std::string closest_hit;
            std::string any_hit;
            std::string intersection;
        };
        std::vector<HitGroup> hit_shaders;
    };

    struct ComputePipelineDescription
    {
        struct Kernel
        {
            std::string name;
            std::string shader;
        };
        std::vector<Kernel> kernels;
        
        struct
        {
            uint32_t size = 0;
            VkShaderStageFlags stages = 0;
        } push_constant_description;
    };

    struct BlitPassDescription
    {
    };

    // 4. Pass 对象
    struct GraphicsPass
    {
        std::vector<TransientResource> attachments;
        GraphicsPassCallback callback;
    };

    struct RaytracingPass
    {
        RaytracingPassCallback callback;
    };

    struct ComputePass
    {
        ComputePassCallback callback;
    };

    struct BlitPass
    {
        std::string srcName;
        std::string dstName;
    };

    struct RenderPass
    {
        std::string name;
        VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        
        // Added storage for keeping descriptor info alive
        struct PrivateInfo
        {
            std::deque<VkDescriptorImageInfo> images;
            std::deque<VkDescriptorBufferInfo> buffers;
            std::deque<VkWriteDescriptorSetAccelerationStructureKHR> as;
            VkAccelerationStructureKHR asHandle;
        };
        std::shared_ptr<PrivateInfo> private_info;

        std::variant<std::monostate, GraphicsPass, RaytracingPass, ComputePass, BlitPass> pass;
    };

    struct RenderPassDescription
    {
        std::string name;
        std::vector<TransientResource> dependencies;
        std::vector<TransientResource> outputs;
        std::variant<std::monostate, GraphicsPipelineDescription, RaytracingPipelineDescription, ComputePipelineDescription, BlitPassDescription> description;
        std::variant<std::monostate, GraphicsPassCallback, RaytracingPassCallback, ComputePassCallback> callback;
    };

    // 5. Specification Wrappers
    struct GraphicsPassSpecification
    {
        std::string Name;
        std::vector<TransientResource> Dependencies;
        std::vector<TransientResource> Outputs;
        std::vector<GraphicsPipelineDescription> Pipelines;
        GraphicsPassCallback Callback;
        std::string ShaderLayout;
    };

    struct RaytracingPassSpecification
    {
        std::string Name;
        std::vector<TransientResource> Dependencies;
        std::vector<TransientResource> Outputs;
        RaytracingPipelineDescription Pipeline;
        RaytracingPassCallback Callback;
        std::string ShaderLayout;
    };

    struct ComputePassSpecification
    {
        std::string Name;
        std::vector<TransientResource> Dependencies;
        std::vector<TransientResource> Outputs;
        ComputePipelineDescription Pipeline;
        ComputePassCallback Callback;
        std::string ShaderLayout;
    };
}
