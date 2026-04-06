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
    None = 0,
    GraphicsSampled,
    ComputeSampled,
    RaytraceSampled,
    StorageRead,
    StorageWrite,
    StorageReadWrite,
    ColorAttachment,
    DepthStencilRead,
    DepthStencilWrite,
    TransferSrc,
    TransferDst
};

struct ResourceRequest
{
    RGResourceHandle handle;
    ResourceUsage usage;
    uint32_t binding = 0xFFFFFFFF;
    VkClearValue clearValue = {{0, 0, 0, 1}};
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
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    bool is_external = false;
};

enum class RGResourceFlagBits
{
    None = 0,
    Persistent = 1 << 0,  // Cross-frame persistence (History)
    External = 1 << 1   // Provided by external system (e.g. Swapchain)
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

struct GraphicsPipelineDescription
{
    std::string name;
    std::string vertex_shader;
    std::string fragment_shader;
    bool depth_test = true;
    bool depth_write = true;
    VkCompareOp depth_compare_op =
        (VkCompareOp)0; // Default 0 will use CH_DEPTH_COMPARE_OP
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    std::vector<uint32_t> specializationConstants;
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
    std::vector<uint32_t> specializationConstants;
};

struct ComputePipelineDescription
{
    struct Kernel
    {
        std::string name;
        std::string shader;
        std::vector<uint32_t> specializationConstants;
    };
    std::vector<Kernel> kernels;
    struct
    {
        uint32_t size = 0;
        VkShaderStageFlags stages = 0;
    } push_constant_description;
};

struct RenderGraphRegistry
{
    RenderGraph& graph;
    struct RenderGraphPass& pass;
    VkImageView GetImageView(RGResourceHandle h);
    VkImage GetImage(RGResourceHandle h);
};

    // --- [NEW] Fluent API Proxy ---
class ResourceHandleProxy
{
public:
    ResourceHandleProxy(RenderGraph& g, RenderGraphPass& p, RGResourceHandle h)
        : graph(g), pass(p), handle(h)
    {
    }

        // Implicit conversion to handle
    operator RGResourceHandle() const
    {
        return handle;
    }

    ResourceHandleProxy& Format(VkFormat format);
    ResourceHandleProxy& Clear(const VkClearColorValue& color);
    ResourceHandleProxy& ClearDepthStencil(float depth, uint32_t stencil = 0);
    ResourceHandleProxy& Persistent();
    ResourceHandleProxy& SaveAsHistory(const std::string& name);

private:
    RenderGraph& graph;
    RenderGraphPass& pass;
    RGResourceHandle handle;
};

struct RenderGraphPass
{
    std::string name;
    bool isCompute = false;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::string>
        shaderNames; // [NEW] Track shaders for documentation/Mermaid
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
} // namespace Chimera