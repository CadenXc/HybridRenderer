#pragma once

#include "RenderGraphCommon.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <deque>

namespace Chimera
{
    class VulkanContext;
    class GraphicsExecutionContext;
    class ComputeExecutionContext;
    class RaytracingExecutionContext;

    class RenderGraph
    {
    public:
        struct PassBuilder
        {
            RenderGraph& graph;
            RenderPass& pass;
            PassBuilder(RenderGraph& g, RenderPass& p) : graph(g), pass(p) {}

            RGResourceHandle Read(const std::string& name);
            RGResourceHandle ReadCompute(const std::string& name);
            RGResourceHandle ReadHistory(const std::string& name);
            
            ResourceHandleProxy Write(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED);
            ResourceHandleProxy WriteStorage(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED);
        };

        RenderGraph(VulkanContext& context, uint32_t w, uint32_t h);
        ~RenderGraph();

        template<typename PassData>
        void AddPass(const std::string& name,
                    std::function<void(PassData&, PassBuilder&)> setup,
                    std::function<void(const PassData&, RenderGraphRegistry&, VkCommandBuffer)> execute)
        {
            auto& pass = m_PassStack.emplace_back();
            pass.name = name;
            pass.width = m_Width;
            pass.height = m_Height;
            auto data = std::make_shared<PassData>();
            PassBuilder builder(*this, pass);
            setup(*data, builder);
            pass.executeFunc = [=](RenderGraphRegistry& reg, VkCommandBuffer cmd) { execute(*data, reg, cmd); };
        }

        template<typename PassData>
        void AddComputePass(const std::string& name,
                           std::function<void(PassData&, PassBuilder&)> setup,
                           std::function<void(const PassData&, ComputeExecutionContext&)> execute)
        {
            auto& pass = m_PassStack.emplace_back();
            pass.name = name;
            pass.isCompute = true;
            pass.width = m_Width;
            pass.height = m_Height;
            auto data = std::make_shared<PassData>();
            PassBuilder builder(*this, pass);
            setup(*data, builder);
            pass.executeFunc = [=](RenderGraphRegistry& reg, VkCommandBuffer cmd) {
                ComputeExecutionContext ctx(reg.graph, reg.pass, cmd);
                execute(*data, ctx);
            };
        }

        void Reset();
        void Compile();
        VkSemaphore Execute(VkCommandBuffer cmd);
        void DestroyResources(bool all = false);

        void SetExternalResource(const std::string& name, VkImage image, VkImageView view, VkImageLayout layout, const ImageDescription& desc);

        RGResourceHandle GetResourceHandle(const std::string& name);
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        
        bool ContainsImage(const std::string& name);
        bool HasHistory(const std::string& name) const;
        const GraphImage& GetImage(const std::string& name) const;
        
        std::vector<std::string> GetDebuggableResources() const;
        const std::vector<PassTiming>& GetLatestTimings() const { return m_LatestTimings; }
        
        void DrawPerformanceStatistics();
        std::string ExportToMermaid() const;

    private:
        struct PhysicalResource
        {
            std::string name; 
            std::string historyName;
            GraphImage image; 
            ImageDescription desc;
            ResourceState currentState; 
            uint32_t firstPass = 0xFFFFFFFF; 
            uint32_t lastPass = 0;
        };
        
        void BuildBarriers(VkCommandBuffer cmd, RenderPass& pass, uint32_t passIdx);
        void BeginPassDebugLabel(VkCommandBuffer cmd, const RenderPass& pass);
        void EndPassDebugLabel(VkCommandBuffer cmd);
        void WriteTimestamp(VkCommandBuffer cmd, uint32_t queryIdx, VkPipelineStageFlags2 stage);
        bool BeginDynamicRendering(VkCommandBuffer cmd, const RenderPass& pass);
        void UpdatePersistentResources(VkCommandBuffer cmd);

    private:
        VulkanContext& m_Context;
        uint32_t m_Width, m_Height;
        std::vector<RenderPass> m_PassStack;
        std::vector<PhysicalResource> m_Resources;
        std::unordered_map<std::string, RGResourceHandle> m_ResourceMap;

        struct HistoryResource
        {
            GraphImage image; 
            ResourceState state;
        };
        std::unordered_map<std::string, HistoryResource> m_HistoryResources;
        std::unordered_map<VkImage, ResourceState> m_ExternalImageStates;
        std::unordered_map<VkImage, ResourceState> m_PhysicalImageStates;

        VkCommandPool m_ComputeCommandPool = VK_NULL_HANDLE;
        VkCommandBuffer m_ComputeCommandBuffer = VK_NULL_HANDLE;
        VkSemaphore m_ComputeFinishedSemaphore = VK_NULL_HANDLE;
        VkSemaphore m_GraphicsWaitSemaphore = VK_NULL_HANDLE;

        VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;
        std::vector<PassTiming> m_LatestTimings;
        std::vector<std::string> m_LastPassNames;
        uint32_t m_PreviousPassCount = 0;

        std::vector<PooledImage> m_ImagePool;

        friend class GraphicsExecutionContext;
        friend class ComputeExecutionContext;
        friend class RaytracingExecutionContext;
        friend struct RenderGraphRegistry;
        friend class ResourceHandleProxy;
    };
}
