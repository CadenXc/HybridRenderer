#pragma once

#include "Renderer/ChimeraCommon.h"
#include "Renderer/Graph/RenderGraphCommon.h"
#include "VulkanContext.h"
#include "Renderer/Resources/ResourceHandle.h"
#include <unordered_map>
#include <memory>
#include <string>

namespace Chimera {

    // 管线物理对象定义
    struct GraphicsPipeline {
        VkPipeline handle = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        GraphicsPipelineDescription description;
    };

    struct RaytracingPipeline {
        VkPipeline handle = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        struct {
            VkStridedDeviceAddressRegionKHR raygen;
            VkStridedDeviceAddressRegionKHR miss;
            VkStridedDeviceAddressRegionKHR hit;
            VkStridedDeviceAddressRegionKHR callable;
        } sbt;
        RaytracingPipelineDescription description;
    };

    struct ComputePipeline {
        VkPipeline handle = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
    };

    class PipelineManager {
    public:
        PipelineManager(std::shared_ptr<VulkanContext> context, ResourceManager& resourceManager);
        ~PipelineManager();

        GraphicsPipeline& GetGraphicsPipeline(const RenderPass& renderPass, const GraphicsPipelineDescription& description);
        RaytracingPipeline& GetRaytracingPipeline(const RenderPass& renderPass, const RaytracingPipelineDescription& description);
        ComputePipeline& GetComputePipeline(const RenderPass& renderPass, const ComputePipelineDescription::Kernel& kernel);
        
        void ClearCache();

    private:
        std::shared_ptr<VulkanContext> m_Context;
        ResourceManager& m_ResourceManager;

        std::unordered_map<std::string, std::unique_ptr<GraphicsPipeline>> m_GraphicsCache;
        std::unordered_map<std::string, std::unique_ptr<RaytracingPipeline>> m_RaytracingCache;
        std::unordered_map<std::string, std::unique_ptr<ComputePipeline>> m_ComputeCache;
    };

}