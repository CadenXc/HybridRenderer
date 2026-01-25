#pragma once

#include "Layer.h"
#include "VulkanContext.h"
#include <vector>

namespace Chimera {

    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer(const std::shared_ptr<VulkanContext>& context);
        ~ImGuiLayer();

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnUIRender() override; // ImGuiLayer 自身的 UI（如果有的话）
        
        // 当窗口大小时，我们需要重建 Framebuffer
        virtual void OnResize(uint32_t width, uint32_t height) override;

        void Begin();
        void End(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        // 允许 Application 暂时屏蔽 ImGui 事件
        void SetBlockEvents(bool block) { m_BlockEvents = block; }
        
        // Application 需要这个 RenderPass 来创建 Framebuffer (如果需要的话)
        VkRenderPass GetRenderPass() const { return m_RenderPass; }

    private:
        void CreateRenderPass();
        void CreateFramebuffers();
        void SetDarkThemeColors(); //美化主题函数

    private:
        std::shared_ptr<VulkanContext> m_Context;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_Framebuffers;
        bool m_BlockEvents = true;
    };

}