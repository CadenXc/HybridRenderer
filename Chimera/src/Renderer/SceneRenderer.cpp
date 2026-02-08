#include "pch.h"
#include "SceneRenderer.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Backend/Renderer.h"
#include "Core/Layer.h"
#include "Core/ImGuiLayer.h"

namespace Chimera {

    SceneRenderer::SceneRenderer(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager, std::shared_ptr<Renderer> renderer, std::shared_ptr<ImGuiLayer> imguiLayer)
        : m_Context(context), m_ResourceManager(resourceManager), m_Renderer(renderer), m_ImGuiLayer(imguiLayer)
    {
    }

    void SceneRenderer::Render(Scene* scene, RenderPath* renderPath, const FrameContext& context, const std::vector<std::shared_ptr<Layer>>& layers)
    {
        // Actual rendering dispatch logic...
    }

}