#pragma once

#include "volk.h"
#include <vk_mem_alloc.h>
#include <variant>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <deque>

namespace Chimera
{
    // Forward declarations
    class GraphicsExecutionContext;
    class RaytracingExecutionContext;
    class ComputeExecutionContext;
    class RenderGraph;
    class Scene;

    // --- 1. Basic Types & Handles ---
    using RGResourceHandle = uint32_t;
    using ResourceHandle = uint32_t;
    static constexpr RGResourceHandle INVALID_RESOURCE = 0xFFFFFFFF;

    enum class ResourceUsage
    {
        None = 0, GraphicsSampled, ComputeSampled, RaytraceSampled,
        StorageRead, StorageWrite, StorageReadWrite,
        ColorAttachment, DepthStencilRead, DepthStencilWrite,
        TransferSrc, TransferDst
    };

    struct ResourceRequest
    {
        RGResourceHandle handle;
        ResourceUsage usage;
        uint32_t binding = 0xFFFFFFFF;
        VkClearValue clearValue = { {0,0,0,1} };
    };

    struct ResourceState
    {
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkAccessFlags2 access = VK_ACCESS_2_NONE;
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    };

    struct GraphImage
    {
        VkImage handle = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkImageView debug_view = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        uint32_t width = 0; uint32_t height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = 0;
        bool is_external = false;
    };

    enum class RGResourceFlagBits
    {
        None = 0,
        Persistent = 1 << 0,  // Cross-frame persistence (History)
        External   = 1 << 1   // Provided by external system (e.g. Swapchain)
    };
    using RGResourceFlags = uint32_t;

    struct ImageDescription
    {
        uint32_t width;
        uint32_t height;
        VkFormat format;
        VkImageUsageFlags usage;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        RGResourceFlags flags = (RGResourceFlags)RGResourceFlagBits::None;
    };

    struct GraphicsPipelineDescription { std::string name, vertex_shader, fragment_shader; bool depth_test = true, depth_write = true; VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT; };
    struct RaytracingPipelineDescription { std::string raygen_shader; std::vector<std::string> miss_shaders; struct HitGroup { std::string closest_hit, any_hit, intersection; }; std::vector<HitGroup> hit_shaders; };
    struct ComputePipelineDescription { struct Kernel { std::string name, shader; }; std::vector<Kernel> kernels; struct { uint32_t size = 0; VkShaderStageFlags stages = 0; } push_constant_description; };

    struct RenderGraphRegistry
    {
        RenderGraph& graph;
        struct RenderPass& pass;
        VkImageView GetImageView(RGResourceHandle h);
        VkImage GetImage(RGResourceHandle h);
    };

    // --- [NEW] Fluent API Proxy ---
    class ResourceHandleProxy
    {
    public:
        ResourceHandleProxy(RenderGraph& g, RenderPass& p, RGResourceHandle h) 
            : graph(g), pass(p), handle(h) {}
        
        // Implicit conversion to handle
        operator RGResourceHandle() const { return handle; }

        ResourceHandleProxy& Format(VkFormat format);
        ResourceHandleProxy& Clear(const VkClearColorValue& color);
        ResourceHandleProxy& ClearDepthStencil(float depth, uint32_t stencil = 0);
        ResourceHandleProxy& Persistent();
        ResourceHandleProxy& SaveAsHistory(const std::string& name);

    private:
        RenderGraph& graph;
        RenderPass& pass;
        RGResourceHandle handle;
    };

    struct RenderPass
    {
        std::string name;
        bool isCompute = false;
        std::vector<std::string> shaderNames; // [NEW] Track shaders for documentation/Mermaid
        std::vector<ResourceRequest> inputs;
        std::vector<ResourceRequest> outputs;
        std::function<void(RenderGraphRegistry&, VkCommandBuffer)> executeFunc;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        std::vector<VkFormat> colorFormats;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    };

    struct PassTiming
    {
        std::string name;
        float durationMS;
    };

    struct PooledImage
    {
        GraphImage image;
        ResourceState state; // [FIX] Track physical state
        int32_t lastUsedPass;
    };

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

        RenderGraph(class VulkanContext& context, uint32_t w, uint32_t h);
        ~RenderGraph();

        template<typename PassData>
        void AddPass(const std::string& name,
                    std::function<void(PassData&, PassBuilder&)> setup,
                    std::function<void(const PassData&, RenderGraphRegistry&, VkCommandBuffer)> execute)
        {
            auto& pass = m_PassStack.emplace_back();
            pass.name = name;
            auto data = std::make_shared<PassData>();
            PassBuilder builder(*this, pass);
            setup(*data, builder);
            pass.executeFunc = [=](RenderGraphRegistry& reg, VkCommandBuffer cmd) { execute(*data, reg, cmd); };
        }

        template<typename PassData>
        void AddComputePass(const std::string& name,
                           std::function<void(PassData&, PassBuilder&)> setup,
                           std::function<void(const PassData&, ComputeExecutionContext&)> execute);

        void Reset();
        void Compile();
        VkSemaphore Execute(VkCommandBuffer cmd);
        void DestroyResources(bool all = false);

        void SetExternalResource(const std::string& name, VkImage image, VkImageView view, VkImageLayout layout, const struct ImageDescription& desc);

        RGResourceHandle GetResourceHandle(const std::string& name);
        uint32_t GetWidth() const
        {
            return m_Width;
        }
        uint32_t GetHeight() const
        {
            return m_Height;
        }
        bool ContainsImage(const std::string& name);
        const GraphImage& GetImage(const std::string& name) const;
        std::vector<std::string> GetDebuggableResources() const;
        const std::vector<PassTiming>& GetLatestTimings() const
        {
            return m_LatestTimings;
        }
        void DrawPerformanceStatistics();
        std::string ExportToMermaid() const;

    private:
        struct PhysicalResource
        {
            std::string name; 
            std::string historyName; // [NEW] Name to store in m_HistoryResources
            GraphImage image; 
            ImageDescription desc;
            ResourceState currentState; 
            uint32_t firstPass = 0xFFFFFFFF; 
            uint32_t lastPass = 0;
        };
        void BuildBarriers(VkCommandBuffer cmd, RenderPass& pass, uint32_t passIdx);

        // --- Execute Helpers ---
        void BeginPassDebugLabel(VkCommandBuffer cmd, const RenderPass& pass);
        void EndPassDebugLabel(VkCommandBuffer cmd);
        void WriteTimestamp(VkCommandBuffer cmd, uint32_t queryIdx, VkPipelineStageFlags2 stage);
        bool BeginDynamicRendering(VkCommandBuffer cmd, const RenderPass& pass);
        void UpdatePersistentResources(VkCommandBuffer cmd);

    private:
        class VulkanContext& m_Context;
        uint32_t m_Width, m_Height;
        std::vector<RenderPass> m_PassStack;
        std::vector<PhysicalResource> m_Resources;
        std::unordered_map<std::string, RGResourceHandle> m_ResourceMap;

        struct HistoryResource
        {
            GraphImage image; ResourceState state;
        };
        std::unordered_map<std::string, HistoryResource> m_HistoryResources;
        std::unordered_map<VkImage, ResourceState> m_ExternalImageStates;
        std::unordered_map<VkImage, ResourceState> m_PhysicalImageStates; // [NEW] Track actual physical layout of images

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