#pragma once
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera
{
    class Shader;

    class GraphicsExecutionContext
    {
    public:
        GraphicsExecutionContext(RenderGraph& graph, RenderGraphPass& pass, VkCommandBuffer cmd);

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

        void SetViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f)
        {
            VkViewport viewport{ x, y, width, height, minDepth, maxDepth };
            vkCmdSetViewport(m_Cmd, 0, 1, &viewport);
        }

        void SetScissor(int32_t offsetX, int32_t offsetY, uint32_t width, uint32_t height)
        {
            VkRect2D scissor{ {offsetX, offsetY}, {width, height} };
            vkCmdSetScissor(m_Cmd, 0, 1, &scissor);
        }

        void DrawMeshes(const struct GraphicsPipelineDescription& desc, class Scene* scene);
        void DispatchRays(const struct RaytracingPipelineDescription& desc);

        VkCommandBuffer GetCommandBuffer()
        {
            return m_Cmd;
        }
        RenderGraph& GetGraph()
        {
            return m_Graph;
        }

    private:
        RenderGraph& m_Graph;
        RenderGraphPass& m_Pass;
        VkCommandBuffer m_Cmd;
        VkPipelineLayout m_ActiveLayout = VK_NULL_HANDLE;
    };
}
