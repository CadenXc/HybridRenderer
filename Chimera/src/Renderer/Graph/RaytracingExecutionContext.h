#pragma once

#include "volk.h"
#include <string>
#include <glm/glm.hpp>
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera
{
    class RenderGraph;
    struct RenderGraphPass;

    class RaytracingExecutionContext
    {
    public:
        RaytracingExecutionContext(RenderGraph& graph, struct RenderGraphPass& pass, VkCommandBuffer cmd);
        virtual ~RaytracingExecutionContext() = default;

        // Support both direct shader name and full description
        void BindPipeline(const std::string& name);
        void BindPipeline(const struct RaytracingPipelineDescription& desc);
        
        void PushConstants(VkShaderStageFlags stages, const void* data, uint32_t size);
        
        template<typename T>
        void PushConstants(VkShaderStageFlags stages, const T& data) { PushConstants(stages, &data, sizeof(T)); }

        void TraceRays(uint32_t w, uint32_t h, uint32_t d = 1);

        VkCommandBuffer GetCommandBuffer() { return m_Cmd; }
        RenderGraph& GetGraph() { return m_Graph; }

    private:
        RenderGraph& m_Graph;
        struct RenderGraphPass& m_Pass;
        VkCommandBuffer m_Cmd;
        VkPipelineLayout m_ActiveLayout = VK_NULL_HANDLE;
    };
}
