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
    RGResourceHandle objectID;
    RGResourceHandle prevObjectID;
    RGResourceHandle prevMotion;
};

    /**
 * @brief Variance estimation step for SVGF (FilterMoments).
 */
struct SVGFVarianceEstimateData
{
    RGResourceHandle inputIllum;
    RGResourceHandle inputMoments;
    RGResourceHandle normal;
    RGResourceHandle motion;
    RGResourceHandle objectID;
    RGResourceHandle outputIllum;
};

    /**
 * @brief A-trous spatial filtering step for SVGF.
 */
struct SVGFAtrousData
{
    RGResourceHandle input;
    RGResourceHandle normal;
    RGResourceHandle motion;
    RGResourceHandle objectID;
    RGResourceHandle materialParams;
    RGResourceHandle output;
};

    /**
 * @brief Final combination step for SVGF.
 */
struct SVGFCombineData
{
    RGResourceHandle current;
    RGResourceHandle output;
    RGResourceHandle albedo;
};


class SVGFPass
{
public:
    struct Config
    {
        std::string inputName = "CurColor";
        std::string prefix = "SVGF";
        std::string historyBaseName = "Accumulated";
        int atrousIterations = 3;
        bool temporalEnabled = true;
        bool spatialEnabled = true;
        bool useAlbedoDemod =
            true; // [NEW] Whether to divide by Albedo before denoising
    };

        /**
     * @brief Adds the entire SVGF pipeline to the graph.
     */
    static void Add(RenderGraph& graph, std::shared_ptr<Scene> scene,
                    const Config& config);
};

    // --- Sub-Pass Classes (Internal use) ---

class SVGFTemporalPass : public ComputePass<SVGFTemporalData>
{
public:
    static constexpr const char* Name = "SVGFTemporalPass";
    SVGFTemporalPass(const SVGFPass::Config& config);
    virtual void Setup(SVGFTemporalData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const SVGFTemporalData& data,
                         ComputeExecutionContext& ctx) override;

private:
    SVGFPass::Config m_Config;
};

class SVGFVarianceEstimatePass : public ComputePass<SVGFVarianceEstimateData>
{
public:
    static constexpr const char* Name = "SVGFVarianceEstimatePass";
    SVGFVarianceEstimatePass(const SVGFPass::Config& config,
                             const std::string& inputIllum,
                             const std::string& inputMoments,
                             const std::string& outputIllum);
    virtual void Setup(SVGFVarianceEstimateData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const SVGFVarianceEstimateData& data,
                         ComputeExecutionContext& ctx) override;

private:
    SVGFPass::Config m_Config;
    std::string m_InputIllum, m_InputMoments, m_OutputIllum;
};

class SVGFAtrousPass : public ComputePass<SVGFAtrousData>
{
public:
    static constexpr const char* Name = "SVGFAtrousPass";
    SVGFAtrousPass(const SVGFPass::Config& config, int iteration,
                   const std::string& inputName, const std::string& outputName,
                   const std::string& historyName = "");
    virtual void Setup(SVGFAtrousData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const SVGFAtrousData& data,
                         ComputeExecutionContext& ctx) override;

private:
    SVGFPass::Config m_Config;
    int m_Iteration;
    std::string m_InputName, m_OutputName, m_HistoryName;
};

class SVGFCombinePass : public ComputePass<SVGFCombineData>
{
public:
    static constexpr const char* Name = "SVGFCombinePass";
    SVGFCombinePass(const SVGFPass::Config& config,
                    const std::string& currentInputColor);
    virtual void Setup(SVGFCombineData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const SVGFCombineData& data,
                         ComputeExecutionContext& ctx) override;

private:
    SVGFPass::Config m_Config;
    std::string m_CurrentInputColor;
};
} // namespace Chimera
