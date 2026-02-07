#pragma once

#include "pch.h"
#include "Renderer/Pipelines/RenderPath.h"
#include "Renderer/Pipelines/ForwardRenderPath.h"
#include "Renderer/Pipelines/RayTracedRenderPath.h"
#include "Renderer/Pipelines/HybridRenderPath.h"

namespace Chimera {

    class RenderPathFactory {
    public:
        static std::unique_ptr<RenderPath> Create(RenderPathType type, std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene, ResourceManager* resourceManager, PipelineManager& pipelineManager, VkDescriptorSetLayout globalLayout) {
            switch (type) {
                case RenderPathType::Forward:
                    return std::make_unique<ForwardRenderPath>(context, scene, resourceManager, pipelineManager, globalLayout);
                case RenderPathType::RayTracing:
                    if (context->IsRayTracingSupported())
                        return std::make_unique<RayTracedRenderPath>(context, scene, resourceManager, pipelineManager, globalLayout);
                    break;
                case RenderPathType::Hybrid:
                    if (context->IsRayTracingSupported())
                        return std::make_unique<HybridRenderPath>(context, scene, resourceManager, pipelineManager, globalLayout);
                    break;
            }

            // Fallback
            return std::make_unique<ForwardRenderPath>(context, scene, resourceManager, pipelineManager, globalLayout);
        }
    };

}
