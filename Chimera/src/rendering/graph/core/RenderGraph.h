#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanCommon.h"
#include "gfx/pipeline/Pipeline.h"
#include "gfx/resources/ResourceManager.h"
#include "gfx/vulkan/VulkanContext.h"
#include "rendering/graph/execution/GraphicsExecutionContext.h"
#include "rendering/graph/execution/ComputeExecutionContext.h"
#include "rendering/graph/execution/RaytracingExecutionContext.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <variant>

namespace Chimera {

    struct ResourceLifetime 
    {
        uint32_t first_pass = 0xFFFFFFFF;
        uint32_t last_pass = 0;
    };

    struct PhysicalImage 
    {
        GraphImage image;
        uint32_t last_used_pass;
    };

    struct PhysicalBuffer
    {
        std::shared_ptr<class Buffer> buffer;
        uint32_t last_used_pass;
    };

    class RenderGraph {
    public:
        RenderGraph(std::shared_ptr<VulkanContext> context, ResourceManager &resource_manager);
        ~RenderGraph();

        void DestroyResources();

        void AddGraphicsPass(const char *render_pass_name, std::vector<TransientResource> dependencies,
            std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines,
            GraphicsPassCallback callback);
        
        void AddRaytracingPass(const char *render_pass_name, std::vector<TransientResource> dependencies,
            std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline,
            RaytracingPassCallback callback);
            
        void AddComputePass(const char *render_pass_name, std::vector<TransientResource> dependencies,
            std::vector<TransientResource> outputs, ComputePipelineDescription pipeline,
            ComputePassCallback callback);

        void Build();
        void Execute(VkCommandBuffer command_buffer, uint32_t resource_idx, uint32_t image_idx);
        
        void GatherPerformanceStatistics();
        void DrawPerformanceStatistics();
        
        void CopyImage(VkCommandBuffer command_buffer, std::string src_image_name, GraphImage dst_image);
        bool ContainsImage(std::string image_name);
        VkFormat GetImageFormat(std::string image_name);
        std::vector<std::string> GetColorAttachments();

    private:
        void CreateGraphicsPass(const RenderPassDescription &pass_description);
        void CreateRaytracingPass(const RenderPassDescription &pass_description);
        void CreateComputePass(const RenderPassDescription &pass_description);

        void FindExecutionOrder();
        void InsertBarriers(VkCommandBuffer command_buffer, RenderPass &render_pass);
        void ExecuteGraphicsPass(VkCommandBuffer command_buffer, uint32_t resource_idx, uint32_t image_idx, RenderPass &render_pass);
        void ExecuteRaytracingPass(VkCommandBuffer command_buffer, uint32_t resource_idx, RenderPass &render_pass);
        void ExecuteComputePass(VkCommandBuffer command_buffer, uint32_t resource_idx, RenderPass &render_pass);
        
        void ActualizeResourceNamed(const std::string& name);
        bool SanityCheck();
        
        VkImageLayout GetRequiredImageLayout(const TransientResource &resource) const;
        VkAccessFlags GetRequiredAccessFlags(const TransientResource &resource) const;
        VkPipelineStageFlags GetRequiredPipelineStageFlags(const TransientResource &resource) const;

    private:
        std::shared_ptr<VulkanContext> m_Context;
        ResourceManager &m_ResourceManager;
        VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;

        std::vector<std::string> m_ExecutionOrder;
        std::unordered_map<std::string, std::vector<std::string>> m_Readers;
        std::unordered_map<std::string, std::vector<std::string>> m_Writers;
        std::unordered_map<std::string, RenderPassDescription> m_PassDescriptions;
        std::unordered_map<std::string, RenderPass> m_Passes;
        std::unordered_map<std::string, GraphicsPipeline> m_GraphicsPipelines;
        std::unordered_map<std::string, RaytracingPipeline> m_RaytracingPipelines;
        std::unordered_map<std::string, ComputePipeline> m_ComputePipelines;
        
        std::unordered_map<std::string, GraphImage> m_Images;
        std::unordered_map<std::string, std::shared_ptr<class Buffer>> m_Buffers;
        std::unordered_map<std::string, ImageAccess> m_ImageAccess;
        std::unordered_map<std::string, double> m_PassTimestamps;
        
        struct ResourceState 
        {
            VkImageLayout layout;
            VkAccessFlags access_flags;
            VkPipelineStageFlags stage_flags;
            bool is_written_in_frame = false;
        };
        std::unordered_map<std::string, ResourceState> m_ResourceStates;

        // Lifetime Analysis
        std::unordered_map<std::string, ResourceLifetime> m_ResourceLifetimes;
        std::unordered_map<std::string, ImageDescription> m_ImageDescriptions;
        std::unordered_map<std::string, BufferDescription> m_BufferDescriptions;

        // Resource Pools for Aliasing
        std::vector<PhysicalImage> m_PhysicalImages;
        std::vector<PhysicalBuffer> m_PhysicalBuffers;

        // Descriptor Cache
        struct DescriptorSetKey
        {
            std::vector<VkDescriptorImageInfo> image_infos;
            bool operator==(const DescriptorSetKey& other) const
            {
                if (image_infos.size() != other.image_infos.size()) return false;
                for (size_t i = 0; i < image_infos.size(); ++i)
                {
                    if (image_infos[i].imageView != other.image_infos[i].imageView ||
                        image_infos[i].sampler != other.image_infos[i].sampler ||
                        image_infos[i].imageLayout != other.image_infos[i].imageLayout) return false;
                }
                return true;
            }
        };

        struct DescriptorSetHash
        {
            size_t operator()(const DescriptorSetKey& key) const
            {
                size_t res = 0;
                for (const auto& info : key.image_infos)
                {
                    res ^= std::hash<void*>{}(info.imageView) + 0x9e3779b9 + (res << 6) + (res >> 2);
                    res ^= std::hash<void*>{}(info.sampler) + 0x9e3779b9 + (res << 6) + (res >> 2);
                }
                return res;
            }
        };

        std::unordered_map<DescriptorSetKey, VkDescriptorSet, DescriptorSetHash> m_DescriptorSetCache;
    };

}