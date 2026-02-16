#pragma once
#include "Core/Layer.h"
#include "Renderer/Backend/VulkanContext.h"
#include <imgui.h>

namespace Chimera
{
    class ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer(const std::shared_ptr<VulkanContext>& context);
        virtual ~ImGuiLayer() override;

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnEvent(Event& e) override;

        void Begin();
        void End(VkCommandBuffer commandBuffer);

        ImTextureID GetTextureID(VkImageView view, VkSampler sampler = VK_NULL_HANDLE);
        void ClearTextureCache();

    private:
        void SetDarkThemeColors();

    private:
        std::shared_ptr<VulkanContext> m_Context;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        std::unordered_map<VkImageView, ImTextureID> m_TextureCache;
    };
}