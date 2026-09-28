#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera::StandardPasses
{
void AddClearPass(RenderGraph& graph, const std::string& name,
                  const VkClearColorValue& clearColor);
} // namespace Chimera::StandardPasses
