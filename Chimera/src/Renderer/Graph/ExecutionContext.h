#pragma once

#include "volk.h"
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera
{
    class RenderGraph;
    struct RenderGraphPass;

    class ExecutionContext
    {
    public:
        ExecutionContext(RenderGraph& graph, RenderGraphPass& pass, VkCommandBuffer cmd);
        virtual ~ExecutionContext() = default;

        // Common Getters
        VkCommandBuffer GetCommandBuffer() const { return m_Cmd; }
        RenderGraph& GetGraph() const { return m_Graph; }

        // Unified PushConstants implementation
        void PushConstants(VkShaderStageFlags stages, const void* data, uint32_t size);

        template<typename T>
        void PushConstants(VkShaderStageFlags stages, const T& data) 
        { 
            PushConstants(stages, &data, sizeof(T)); 
        }

    protected:
        RenderGraph& m_Graph;
        RenderGraphPass& m_Pass;
        VkCommandBuffer m_Cmd;
        VkPipelineLayout m_ActiveLayout = VK_NULL_HANDLE;
    };
}
