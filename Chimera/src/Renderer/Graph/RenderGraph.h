#pragma once
#include "RenderGraphCommon.h"
#include <unordered_map>

namespace Chimera {

    class RenderGraph {
    public:
        RenderGraph(VulkanContext& context, ResourceManager& rm, PipelineManager& pm, uint32_t w, uint32_t h);
        ~RenderGraph();
        
        void AddGraphicsPass(const GraphicsPassSpecification& s);
        void AddRaytracingPass(const RaytracingPassSpecification& s);
        void AddComputePass(const ComputePassSpecification& s);
        void AddBlitPass(const std::string& n, const std::string& s, const std::string& d, VkFormat sf, VkFormat df);
        
        void Build();
        void Execute(VkCommandBuffer cmd, uint32_t rIdx, uint32_t iIdx);
        
        void RegisterExternalResource(const std::string& n, const ImageDescription& d);
        void SetExternalResource(const std::string& n, VkImage h, VkImageView v, VkImageLayout cl, VkAccessFlags ca, VkPipelineStageFlags cs);
        void DestroyResources(bool all = false);
        
        VkFormat GetImageFormat(std::string n);
        bool ContainsImage(std::string n);
        const GraphImage& GetImage(std::string n) const;
        ImageAccess& GetImageAccess(const std::string& n);
        std::vector<std::string> GetColorAttachments() const;
        void DrawPerformanceStatistics();

    private:
        void CreateGraphicsPass(RenderPassDescription& d);
        void CreateRaytracingPass(RenderPassDescription& d);
        void CreateComputePass(RenderPassDescription& d);
        void ExecuteGraphicsPass(VkCommandBuffer cmd, uint32_t rIdx, uint32_t iIdx, RenderPass& p);
        void ExecuteRaytracingPass(VkCommandBuffer cmd, uint32_t rIdx, RenderPass& p);
        void ExecuteComputePass(VkCommandBuffer cmd, uint32_t rIdx, RenderPass& p);
        void ExecuteBlitPass(VkCommandBuffer cmd, RenderPass& p);

        VulkanContext& m_Context; 
        ResourceManager& m_ResourceManager; 
        PipelineManager& m_PipelineManager;
        uint32_t m_Width, m_Height;
        
        std::unordered_map<std::string, RenderPassDescription> m_PassDescriptions;
        std::unordered_map<std::string, RenderPass> m_Passes;
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