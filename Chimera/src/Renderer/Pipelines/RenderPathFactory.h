#pragma once

#include "RenderPath.h"
#include <memory>

namespace Chimera
{
    class RenderPathFactory
    {
    public:
        static std::unique_ptr<RenderPath> Create(RenderPathType type, std::shared_ptr<class VulkanContext> context);
    };
}