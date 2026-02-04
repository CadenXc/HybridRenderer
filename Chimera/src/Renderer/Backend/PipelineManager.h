#pragma once

#include "pch.h"
#include "Renderer/Backend/VulkanCommon.h"
#include <unordered_map>
#include <string>
#include <memory>

namespace Chimera {

    class VulkanContext;
    class ResourceManager;
    struct RenderPass;

    class PipelineManager
    {
    public:
        PipelineManager(std::shared_ptr<VulkanContext> context, ResourceManager& resourceManager);
        ~PipelineManager();

        GraphicsPipeline& GetGraphicsPipeline(const RenderPass& renderPass, const GraphicsPipelineDescription& description);
        RaytracingPipeline& GetRaytracingPipeline(const RenderPass& renderPass, const RaytracingPipelineDescription& description);
        ComputePipeline& GetComputePipeline(const RenderPass& renderPass, const PushConstantDescription& pushConstants, const ComputeKernel& kernel);

        void ClearCache(); 
        bool CheckForShaderUpdates(); // Monitors .spv files
        bool CheckForSourceUpdates(); // Monitors .vert/.frag source files

    private:
        std::shared_ptr<VulkanContext> m_Context;
        ResourceManager& m_ResourceManager;

        std::unordered_map<std::string, std::unique_ptr<GraphicsPipeline>> m_GraphicsCache;
        std::unordered_map<std::string, std::unique_ptr<RaytracingPipeline>> m_RaytracingCache;
        std::unordered_map<std::string, std::unique_ptr<ComputePipeline>> m_ComputeCache;

        std::unordered_map<std::string, std::filesystem::file_time_type> m_ShaderTimestamps;
        std::unordered_map<std::string, std::filesystem::file_time_type> m_SourceTimestamps;
    };

}
