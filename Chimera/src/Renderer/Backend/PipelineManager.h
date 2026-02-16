#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Shader.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>

namespace Chimera
{
    struct GraphicsPipeline
    {
        VkPipeline handle = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        GraphicsPipelineDescription description;
        std::vector<const Shader*> shaders; // [NEW] 存储完整 Shader 链用于反射对齐
    };

    struct RaytracingPipeline
    {
        VkPipeline handle = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        RaytracingPipelineDescription description;
        std::vector<const Shader*> shaders; // [NEW] 包含 Raygen, Miss, Hit
        
        struct SBT { VkStridedDeviceAddressRegionKHR raygen{}, miss{}, hit{}, callable{}; } sbt;
        std::unique_ptr<class Buffer> sbt_buffer;
    };

    struct ComputePipeline
    {
        VkPipeline handle = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        std::vector<const Shader*> shaders; // [NEW]
    };

    class PipelineManager
    {
    public:
        PipelineManager();
        ~PipelineManager();

        void ClearCache();

        GraphicsPipeline& GetGraphicsPipeline(const std::vector<VkFormat>& colorFormats, VkFormat depthFormat, const GraphicsPipelineDescription& desc);
        RaytracingPipeline& GetRaytracingPipeline(const RaytracingPipelineDescription& desc);
        ComputePipeline& GetComputePipeline(const ComputePipelineDescription::Kernel& kernel);

        // 获取基于 Shader 链的统一布局
        VkPipelineLayout GetReflectionLayout(const std::vector<const Shader*>& shaders);
        VkDescriptorSetLayout GetSet2Layout(const std::vector<const Shader*>& shaders);

        static PipelineManager& Get() { return *s_Instance; }

    private:
        size_t CalculateShaderHash(const std::vector<const Shader*>& shaders);
        static VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code);

    private:
        static PipelineManager* s_Instance;
        std::unordered_map<std::string, std::unique_ptr<GraphicsPipeline>> m_GraphicsCache;
        std::unordered_map<std::string, std::unique_ptr<RaytracingPipeline>> m_RaytracingCache;
        std::unordered_map<std::string, std::unique_ptr<ComputePipeline>> m_ComputeCache;
        
        std::unordered_map<size_t, VkPipelineLayout> m_LayoutCache;
        std::unordered_map<size_t, VkDescriptorSetLayout> m_Set2LayoutCache;
    };
}
