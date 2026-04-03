#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <string>
#include <memory>

namespace Chimera
{
    class Scene;

    struct CompositionPassData 
    { 
        RGResourceHandle albedo;
        RGResourceHandle normal;
        RGResourceHandle material;
        RGResourceHandle motion;
        RGResourceHandle depth;
        RGResourceHandle emissive;
        
        RGResourceHandle gi_raw;
        RGResourceHandle reflection_raw;
        RGResourceHandle shadow_raw;
        RGResourceHandle ao_raw;
        RGResourceHandle shadow_debug_raw; // NEW: Always points to the raw, un-denoised signal
        
        RGResourceHandle output; 
    };

    class CompositionPass : public GraphicsPass<CompositionPassData>
    {
    public:
        using PassData = CompositionPassData;
        static constexpr const char* Name = "Composition";

        struct Config
        {
            std::string shadowName = "Shadow_Filtered_Final";
            std::string aoName = "AO_Filtered_Final";
            std::string reflectionName = "Refl_Filtered_Final";
            std::string giName = "GI_Filtered_Final";
        };

        using Data = PassData;

        CompositionPass(const Config& config);

        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) override;
        virtual void Execute(const PassData& data, GraphicsExecutionContext& ctx) override;

    private:
        Config m_Config;
    };
}
