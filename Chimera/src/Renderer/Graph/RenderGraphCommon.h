#pragma once

#include "volk.h"
#include <vk_mem_alloc.h>
#include <variant>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

namespace Chimera
{
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
        VkAccessFlags access = 0;
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
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

    // --- 2. Pipeline Descriptions ---
    struct GraphicsPipelineDescription { std::string name, vertex_shader, fragment_shader; bool depth_test = true, depth_write = true; VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT; };
    struct RaytracingPipelineDescription { std::string raygen_shader; std::vector<std::string> miss_shaders; struct HitGroup { std::string closest_hit, any_hit, intersection; }; std::vector<HitGroup> hit_shaders; };
    struct ComputePipelineDescription { struct Kernel { std::string name, shader; }; std::vector<Kernel> kernels; struct { uint32_t size = 0; VkShaderStageFlags stages = 0; } push_constant_description; };

    struct GraphicsPass { std::vector<struct TransientResource> attachments; };
    struct RaytracingPass {};
    struct ComputePass {};
    struct BlitPass { std::string srcName, dstName; };

    // --- 3. RenderGraph Core Structure ---
    class RenderGraph;

    struct RenderGraphRegistry
    {
        RenderGraph& graph;
        struct RenderPass& pass;
        VkImageView GetImageView(RGResourceHandle h);
        VkImage GetImage(RGResourceHandle h);
    };

    struct RenderPass
    {
        std::string name;
        std::vector<ResourceRequest> inputs;
        std::vector<ResourceRequest> outputs;
        std::function<void(RenderGraphRegistry&, VkCommandBuffer)> executeFunc;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        std::vector<VkFormat> colorFormats;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    };

    class RenderGraph
    {
    public:
        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        struct PassBuilder
        {
            RenderGraph& graph;
            RenderPass& pass;
            PassBuilder(RenderGraph& g, RenderPass& p) : graph(g), pass(p) {}
            RGResourceHandle Read(const std::string& name);
            RGResourceHandle Write(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED);
            RGResourceHandle WriteStorage(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED);
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

        void Reset();
        void Compile();
        void Execute(VkCommandBuffer cmd);
        void DestroyResources(bool all = false);

        RGResourceHandle GetResourceHandle(const std::string& name);
        void ImportExternalResource(const std::string& name, VkImage image, VkImageView view, VkFormat format);
        
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        bool ContainsImage(const std::string& name);
        const GraphImage& GetImage(const std::string& name) const;
        std::vector<std::string> GetColorAttachments() const;
        void DrawPerformanceStatistics();

    private:
        struct PhysicalResource { std::string name; GraphImage image; ResourceState currentState; bool isExternal = false; };
        void BuildBarriers(VkCommandBuffer cmd, RenderPass& pass);
        void BakeDescriptorSet(RenderPass& pass);

    private:
        class VulkanContext& m_Context;
        uint32_t m_Width, m_Height;
        std::vector<RenderPass> m_PassStack;
        std::vector<PhysicalResource> m_Resources;
        std::unordered_map<std::string, RGResourceHandle> m_ResourceMap;
        VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;
        friend struct RenderGraphRegistry;
    };
}