#pragma once

#include "RenderGraphCommon.h"
#include <string>
#include <vector>

namespace Chimera
{
    class RenderGraph;
    struct RenderPass;
    class Shader;

    /**
     * @brief 自动化执行上下文：统一处理管线、描述符绑定、以及底层绘制指令
     */
    class GraphicsExecutionContext
    {
    public:
        GraphicsExecutionContext(RenderGraph& graph, RenderPass& pass, VkCommandBuffer cmd);

        // --- 核心绘制方法 ---
        void DrawMeshes(const GraphicsPipelineDescription& desc, class Scene* scene);
        void DispatchRays(const RaytracingPipelineDescription& desc);

        // --- 底层封装 (供 Scene 等类内部调用) ---
        VkCommandBuffer GetCommandBuffer() const { return m_Cmd; }
        VkPipelineLayout GetActiveLayout() const { return m_ActiveLayout; } // [NEW] 用于状态同步
        
        template<typename T>
        void PushConstants(VkShaderStageFlags stages, const T& data)
        {
            if (m_ActiveLayout != VK_NULL_HANDLE)
            {
                vkCmdPushConstants(m_Cmd, m_ActiveLayout, stages, 0, sizeof(T), &data);
            }
        }

        void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
        {
            vkCmdDrawIndexed(m_Cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        }

    private:
        void BindPipelineAndDescriptorSets(VkPipelineBindPoint bindPoint, VkPipeline handle, VkPipelineLayout layout, const std::vector<const Shader*>& shaders);

    private:
        RenderGraph& m_Graph;
        RenderPass& m_Pass;
        VkCommandBuffer m_Cmd;
        VkPipelineLayout m_ActiveLayout = VK_NULL_HANDLE; // [NEW]
    };
}
