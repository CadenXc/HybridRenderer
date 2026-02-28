#pragma once
#include "Renderer/Graph/RenderGraphCommon.h"
#include <string>

namespace Chimera::CompositionPass
{
    struct Config
    {
        std::string shadowName = "Shadow_Filtered_4";
        std::string reflectionName = "Refl_Filtered_4";
        std::string giName = "GI_Filtered_4";
    };

    void AddToGraph(RenderGraph& graph, const Config& config);
}
