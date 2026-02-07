#include "pch.h"
#include "RenderPath.h"
#include "Core/Application.h"
#include "Core/ImGuiLayer.h"
#include "Renderer/Backend/PipelineManager.h"

namespace Chimera {

    void RenderPath::RebuildGraph()
    {
        // CH_CORE_INFO("RenderPath: RebuildGraph ENTER. Member dimensions: {}x{}", m_Width, m_Height);
        vkDeviceWaitIdle(m_Context->GetDevice());
        
        m_PipelineManager.ClearCache();
        m_ResourceManager->ResetTransientDescriptorPool();
        m_RenderGraph.reset(); // Destroy old graph first
        
        // Clear ImGui texture cache as old views will be destroyed
        if (Application::Get().GetImGuiLayer())
            Application::Get().GetImGuiLayer()->ClearTextureCache();

        uint32_t w = m_Width;
        uint32_t h = m_Height;

        // Fallback to swapchain extent if viewport is not set
        if (w == 0 || h == 0) {
            auto extent = m_Context->GetSwapChainExtent();
            w = extent.width;
            h = extent.height;
        }

        // CH_CORE_INFO("RenderPath: Rebuilding graph with {}x{}", w, h);

        // Re-create graph object
        m_RenderGraph = std::make_unique<RenderGraph>(*m_Context, *m_ResourceManager, m_PipelineManager, w, h);
        SetupGraph(*m_RenderGraph);
    }

}