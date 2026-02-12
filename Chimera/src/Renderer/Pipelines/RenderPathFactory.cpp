#include "pch.h"
#include "RenderPathFactory.h"
#include "ForwardRenderPath.h"
#include "HybridRenderPath.h"
#include "RayTracedRenderPath.h"

namespace Chimera
{
    std::unique_ptr<RenderPath> RenderPathFactory::Create(RenderPathType type, std::shared_ptr<VulkanContext> context, std::shared_ptr<Scene> scene)
    {
        switch (type)
        {
            case RenderPathType::Forward:   return std::make_unique<ForwardRenderPath>(*context, scene);
            case RenderPathType::Hybrid:    return std::make_unique<HybridRenderPath>(*context, scene);
            case RenderPathType::RayTracing: return std::make_unique<RayTracedRenderPath>(*context, scene);
        }
        return nullptr;
    }
}