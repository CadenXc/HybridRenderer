#pragma once

#include "RenderGraphCommon.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

namespace Chimera
{
    class RenderGraph
    {
    public:
        RenderGraph(class VulkanContext& context, uint32_t w, uint32_t h);
        ~RenderGraph();
        
        // --- Pass Registration ---
        void AddGraphicsPass(const GraphicsPassSpecification& s);
        void AddRaytracingPass(const RaytracingPassSpecification& s);
        void AddComputePass(const ComputePassSpecification& s);
        void AddBlitPass(const std::string& n, const std::string& s, const std::string& d, VkFormat sf, VkFormat df);
        
        // --- Lifecycle ---
        void Build();
        void Execute(VkCommandBuffer cmd, uint32_t rIdx, uint32_t iIdx);
        void DestroyResources(bool all = false);

        // --- Resource Management ---
        void RegisterExternalResource(const std::string& n, const ImageDescription& d);
        void SetExternalResource(const std::string& n, VkImage h, VkImageView v, VkImageLayout cl, VkAccessFlags ca, VkPipelineStageFlags cs);
        
        // --- Getters ---
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        VkFormat GetImageFormat(std::string n);
        bool ContainsImage(std::string n);
        const GraphImage& GetImage(std::string n) const;
        ImageAccess& GetImageAccess(const std::string& n);
        std::vector<std::string> GetColorAttachments() const;

        const std::unordered_map<std::string, struct ComputePipeline*>& GetComputePipelines() const
        {
            return m_ComputePipelines;
        }

        void DrawPerformanceStatistics();

    private:
        // --- Internal Pass Creation ---
        void CreateGraphicsPass(struct RenderPassDescription& d);
        void CreateRaytracingPass(struct RenderPassDescription& d);
        void CreateComputePass(struct RenderPassDescription& d);

        // --- Internal Pass Execution ---
        void ExecuteGraphicsPass(VkCommandBuffer cmd, uint32_t rIdx, uint32_t iIdx, struct RenderPass& p);
        void ExecuteRaytracingPass(VkCommandBuffer cmd, uint32_t rIdx, struct RenderPass& p);
        void ExecuteComputePass(VkCommandBuffer cmd, uint32_t rIdx, struct RenderPass& p);
        void ExecuteBlitPass(VkCommandBuffer cmd, struct RenderPass& p);

    private:
        class VulkanContext& m_Context; 
        uint32_t m_Width, m_Height;
        
        std::unordered_map<std::string, struct RenderPassDescription> m_PassDescriptions;
        std::unordered_map<std::string, struct RenderPass> m_Passes;
        std::vector<std::string> m_ExecutionOrder;
        
        std::unordered_map<std::string, GraphImage> m_Images;
        std::unordered_map<std::string, ImageAccess> m_ImageAccess;
        
        std::unordered_map<std::string, struct GraphicsPipeline*> m_GraphicsPipelines;
        std::unordered_map<std::string, struct RaytracingPipeline*> m_RaytracingPipelines;
        std::unordered_map<std::string, struct ComputePipeline*> m_ComputePipelines;
        
        std::vector<std::vector<VkDescriptorImageInfo>> m_SamplerArrays;
        VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;
    };
}