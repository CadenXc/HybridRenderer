#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <string>
#include <memory>

namespace Chimera
{
    class Scene;

    /**
     * @brief Temporal accumulation step for SVGF.
     */
    struct SVGFTemporalData
    {
        RGResourceHandle cur; 
        RGResourceHandle motion; 
        RGResourceHandle history; 
        RGResourceHandle historyMoments; 
        RGResourceHandle output; 
        RGResourceHandle outMoments; 
        RGResourceHandle depth;
        RGResourceHandle normal;
        RGResourceHandle prevDepth;
        RGResourceHandle prevNormal;
    };

    /**
     * @brief A-trous spatial filtering step for SVGF.
     */
    struct SVGFAtrousData
    {
        RGResourceHandle input; 
        RGResourceHandle moments; 
        RGResourceHandle normal; 
        RGResourceHandle depth; 
        RGResourceHandle output; 
    };

    /**
     * @brief Final combination step for SVGF.
     */
    struct SVGFCombineData
    {
        RGResourceHandle current; 
        RGResourceHandle history; 
        RGResourceHandle moments; 
        RGResourceHandle output; 
    };

    class SVGFPass
    {
    public:
        struct Config
        {
            std::string inputName = "CurColor";
            std::string prefix = "SVGF";
            std::string historyBaseName = "Accumulated";
            int atrousIterations = 5;
        };

        /**
         * @brief Adds the entire SVGF pipeline (Temporal + Atrous loop + Combine) to the graph.
         * Internally uses the new class-based RenderPass system.
         */
        static void Add(RenderGraph& graph, std::shared_ptr<Scene> scene, const Config& config);
    };

    // --- Sub-Pass Classes (Internal use) ---

    class SVGFTemporalPass : public ComputePass<SVGFTemporalData>
    {
    public:
        static constexpr const char* Name = "SVGFTemporalPass";
        SVGFTemporalPass(const SVGFPass::Config& config);
        virtual void Setup(SVGFTemporalData& data, RenderGraph::PassBuilder& builder) override;
        virtual void Execute(const SVGFTemporalData& data, ComputeExecutionContext& ctx) override;
    private:
        SVGFPass::Config m_Config;
    };

    class SVGFAtrousPass : public ComputePass<SVGFAtrousData>
    {
    public:
        static constexpr const char* Name = "SVGFAtrousPass";
        SVGFAtrousPass(const SVGFPass::Config& config, int iteration, const std::string& inputName, const std::string& outputName, const std::string& momentsName);
        virtual void Setup(SVGFAtrousData& data, RenderGraph::PassBuilder& builder) override;
        virtual void Execute(const SVGFAtrousData& data, ComputeExecutionContext& ctx) override;
    private:
        SVGFPass::Config m_Config;
        int m_Iteration;
        std::string m_InputName, m_OutputName, m_MomentsName;
    };

    class SVGFCombinePass : public ComputePass<SVGFCombineData>
    {
    public:
        static constexpr const char* Name = "SVGFCombinePass";
        SVGFCombinePass(const SVGFPass::Config& config, const std::string& currentInputColor, const std::string& temporalMomentsName);
        virtual void Setup(SVGFCombineData& data, RenderGraph::PassBuilder& builder) override;
        virtual void Execute(const SVGFCombineData& data, ComputeExecutionContext& ctx) override;
    private:
        SVGFPass::Config m_Config;
        std::string m_CurrentInputColor, m_TemporalMomentsName;
    };
}
