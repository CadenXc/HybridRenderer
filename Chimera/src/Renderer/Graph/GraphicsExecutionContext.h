#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera
{
    class Shader;

    class GraphicsExecutionContext
    {
    public:
        GraphicsExecutionContext(RenderGraph& graph, struct RenderPass& pass, VkCommandBuffer cmd);
        
        void BindPipeline(const struct GraphicsPipelineDescription& desc);
        void BindPipelineAndDescriptorSets(VkPipelineBindPoint bindPoint, VkPipeline handle, VkPipelineLayout layout, const std::vector<const Shader*>& shaders);
        
        template<typename T>
        void PushConstants(VkShaderStageFlags stages, const T& constants)
        {
            if (m_ActiveLayout != VK_NULL_HANDLE)
            {
                vkCmdPushConstants(m_Cmd, m_ActiveLayout, VK_SHADER_STAGE_ALL, 0, sizeof(T), &constants);
            }
        }

        void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
        {
            vkCmdDrawIndexed(m_Cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        }

        void BindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets)
        {
            vkCmdBindVertexBuffers(m_Cmd, firstBinding, bindingCount, pBuffers, pOffsets);
        }

        void BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
        {
            vkCmdBindIndexBuffer(m_Cmd, buffer, offset, indexType);
        }

        void DrawMeshes(const struct GraphicsPipelineDescription& desc, class Scene* scene);
        void DispatchRays(const struct RaytracingPipelineDescription& desc);

        VkCommandBuffer GetCommandBuffer() { return m_Cmd; }
        RenderGraph& GetGraph() { return m_Graph; }

    private:
        RenderGraph& m_Graph;
        struct RenderPass& m_Pass;
        VkCommandBuffer m_Cmd;
        VkPipelineLayout m_ActiveLayout = VK_NULL_HANDLE;
    };
}
