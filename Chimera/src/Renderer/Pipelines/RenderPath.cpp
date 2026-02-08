#include "pch.h"
#include "RenderPath.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Graph/RenderGraph.h"

namespace Chimera {

    RenderPath::RenderPath(std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager)
        : m_Context(context), m_Scene(scene), m_ResourceManager(resourceManager), m_PipelineManager(pipelineManager)
    {
    }

    RenderPath::~RenderPath()
    {
    }

}