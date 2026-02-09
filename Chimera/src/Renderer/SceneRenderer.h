#pragma once

#include "pch.h"
#include "Renderer/ChimeraCommon.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Resources/ResourceManager.h"
#include <glm/glm.hpp>

namespace Chimera
{
    class Scene;
    class Renderer;
    class Layer;
    class ImGuiLayer;

    struct FrameContext
    {
        glm::mat4 View{ 1.0f };
        glm::mat4 Projection{ 1.0f };
        glm::vec3 CameraPosition{ 0.0f };
        float Time{ 0.0f };
        float DeltaTime{ 0.0f };
        glm::vec2 ViewportSize{ 1600.0f, 900.0f };
        uint32_t FrameIndex{ 0 };
    };

    class SceneRenderer
    {
    public:
        SceneRenderer(std::shared_ptr<VulkanContext> context, std::shared_ptr<ImGuiLayer> imguiLayer);
        ~SceneRenderer() = default;

        void Render(Scene* scene, RenderPath* renderPath, const FrameContext& context, const std::vector<std::shared_ptr<Layer>>& layers);

    private:
        std::shared_ptr<VulkanContext> m_Context;
        std::shared_ptr<ImGuiLayer> m_ImGuiLayer;

        glm::mat4 m_LastView{ 1.0f };
        glm::mat4 m_LastProj{ 1.0f };
    };
}