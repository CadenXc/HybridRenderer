#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"

namespace Chimera::PostProcessPass
{
    void AddToGraph(RenderGraph& graph, const std::string& inputName = "TAAOutput");
}
