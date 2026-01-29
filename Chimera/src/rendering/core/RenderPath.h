#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanContext.h"
#include "core/scene/Scene.h"
#include "gfx/resources/ResourceManager.h"

namespace Chimera {

    class RenderPath {
    public:
        RenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager)
            : m_Context(context), m_Scene(scene), m_ResourceManager(resourceManager) {}
        
        virtual ~RenderPath() = default;

        virtual void Init() = 0;
        virtual void OnSceneUpdated() {} 
        virtual void OnImGui() {}
        virtual void Render(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t imageIndex, 
                            VkDescriptorSet globalDescriptorSet, const std::vector<VkImage>& swapChainImages,
                            std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr) = 0;

    protected:
        std::shared_ptr<VulkanContext> m_Context;
        std::shared_ptr<Scene> m_Scene;
        ResourceManager* m_ResourceManager;
    };

}

