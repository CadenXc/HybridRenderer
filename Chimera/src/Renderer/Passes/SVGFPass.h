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
    RGResourceHandle material;
    RGResourceHandle prevMaterial;
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
    RGResourceHandle material;
    RGResourceHandle outputIllum;
    RGResourceHandle outputMoments;
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
    RGResourceHandle material;
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

    /**
 * @brief Variance blur step for SVGF (Pre-Atrous).
 */
struct SVGFVarianceBlurData
{
    RGResourceHandle inputMoments;
    RGResourceHandle outputMoments;
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
                             const std::string& outputIllum,
                             const std::string& outputMoments);
    virtual void Setup(SVGFVarianceEstimateData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const SVGFVarianceEstimateData& data,
                         ComputeExecutionContext& ctx) override;

private:
    SVGFPass::Config m_Config;
    std::string m_InputIllum, m_InputMoments, m_OutputIllum, m_OutputMoments;
};

class SVGFVarianceBlurPass : public ComputePass<SVGFVarianceBlurData>
{
public:
    static constexpr const char* Name = "SVGFVarianceBlurPass";
    SVGFVarianceBlurPass(const SVGFPass::Config& config,
                         const std::string& inputMoments,
                         const std::string& outputMoments);
    virtual void Setup(SVGFVarianceBlurData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const SVGFVarianceBlurData& data,
                         ComputeExecutionContext& ctx) override;

private:
    SVGFPass::Config m_Config;
    std::string m_InputMoments, m_OutputMoments;
};

class SVGFAtrousPass : public ComputePass<SVGFAtrousData>
{
public:
    static constexpr const char* Name = "SVGFAtrousPass";
    SVGFAtrousPass(const SVGFPass::Config& config, int iteration,
                   const std::string& inputName, const std::string& outputName,
                   const std::string& momentsName,
                   const std::string& historyName = "");
    virtual void Setup(SVGFAtrousData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const SVGFAtrousData& data,
                         ComputeExecutionContext& ctx) override;

private:
    SVGFPass::Config m_Config;
    int m_Iteration;
    std::string m_InputName, m_OutputName, m_MomentsName, m_HistoryName;
};

class SVGFCombinePass : public ComputePass<SVGFCombineData>
{
public:
    static constexpr const char* Name = "SVGFCombinePass";
    SVGFCombinePass(const SVGFPass::Config& config,
                    const std::string& currentInputColor,
                    const std::string& temporalMomentsName);
    virtual void Setup(SVGFCombineData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const SVGFCombineData& data,
                         ComputeExecutionContext& ctx) override;

private:
    SVGFPass::Config m_Config;
    std::string m_CurrentInputColor, m_TemporalMomentsName;
};
} // namespace Chimera
