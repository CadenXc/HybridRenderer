#include "pch.h"
#include "RenderPathFactory.h"
#include "ForwardRenderPath.h"
#include "HybridRenderPath.h"
#include "RayTracedRenderPath.h"
#include "RayQueryRenderPath.h"

namespace Chimera
{
    std::unique_ptr<RenderPath> RenderPathFactory::Create(RenderPathType type, std::shared_ptr<VulkanContext> context)
    {
        switch (type)
        {
            case RenderPathType::Forward:   return std::make_unique<ForwardRenderPath>(*context);
            case RenderPathType::Hybrid:    return std::make_unique<HybridRenderPath>(*context);
            case RenderPathType::RayTracing: return std::make_unique<RayTracedRenderPath>(*context);
            case RenderPathType::RayQuery:  return std::make_unique<RayQueryRenderPath>(*context);
        }
        return nullptr;
    }
}