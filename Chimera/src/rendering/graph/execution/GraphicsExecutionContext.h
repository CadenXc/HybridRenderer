#pragma once
#include "pch.h"
#include "gfx/resources/ResourceManager.h"
#include "gfx/vulkan/VulkanCommon.h"

namespace Chimera
{
    class GraphicsExecutionContext
    {
    public:
        GraphicsExecutionContext(VkCommandBuffer commandBuffer, ResourceManager &resourceManager, GraphicsPipeline &pipeline)
            : m_CommandBuffer(commandBuffer), m_ResourceManager(resourceManager), m_Pipeline(pipeline)
        {
        }

        void BindGlobalVertexAndIndexBuffers()
        {
            // TODO: Implement using Scene or ResourceManager if they hold global buffers
        }

        void BindVertexBuffer(VkBuffer buffer, VkDeviceSize offset)
        {
            vkCmdBindVertexBuffers(m_CommandBuffer, 0, 1, &buffer, &offset);
        }
        
        void BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
        {
            vkCmdBindIndexBuffer(m_CommandBuffer, buffer, offset, type);
        }

        void SetScissor(VkRect2D scissor)
        {
            vkCmdSetScissor(m_CommandBuffer, 0, 1, &scissor);
        }

        void SetViewport(VkViewport viewport)
        {
            vkCmdSetViewport(m_CommandBuffer, 0, 1, &viewport);
        }

        void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance)
        {
            vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        }

        void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
        {
            vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
        }

        template<typename T>
        void PushConstants(T &pushConstants)
        {
            vkCmdPushConstants(m_CommandBuffer, m_Pipeline.layout, m_Pipeline.description.push_constants.shader_stage, 0, m_Pipeline.description.push_constants.size, &pushConstants);
        }

    private:
        VkCommandBuffer m_CommandBuffer;
        ResourceManager &m_ResourceManager;
        GraphicsPipeline &m_Pipeline;
    };
}