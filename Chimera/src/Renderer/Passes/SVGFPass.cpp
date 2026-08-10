#include "pch.h"
#include "SVGFPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ComputeExecutionContext.h"

namespace Chimera
{
    // --- Temporal Pass ---
SVGFTemporalPass::SVGFTemporalPass(const SVGFPass::Config& config)
    : m_Config(config)
{
}
void SVGFTemporalPass::Setup(SVGFTemporalData& data,
                             RenderGraph::PassBuilder& builder)
{
    data.cur = builder.ReadCompute(m_Config.inputName);
    data.motion = builder.ReadCompute(RS::Motion);
    data.history =
        builder.ReadHistorySafe(m_Config.historyBaseName, m_Config.inputName);
    data.historyMoments = builder.ReadHistorySafe(m_Config.prefix + "Moments",
                                                  m_Config.inputName);

    auto outputProxy = builder.WriteStorage(m_Config.prefix + "_TemporalColor")
                           .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    if (!m_Config.spatialEnabled)
    {
        outputProxy.SaveAsHistory(m_Config.historyBaseName);
    }
    data.output = outputProxy;

    data.outMoments = builder.WriteStorage(m_Config.prefix + "_TemporalMoments")
                          .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                          .SaveAsHistory(m_Config.prefix + "Moments");
    data.depth = builder.ReadCompute(RS::Depth);
    data.normal = builder.ReadCompute(RS::Normal);
    data.prevDepth = builder.ReadHistorySafe(RS::Depth, RS::Depth);
    data.prevNormal = builder.ReadHistorySafe(RS::Normal, RS::Normal);
    data.objectID = builder.ReadCompute(RS::ObjectID);
    data.prevObjectID = builder.ReadHistorySafe(RS::ObjectID, RS::ObjectID);
    data.prevMotion = builder.ReadHistorySafe(RS::Motion, RS::Motion);
    builder.ReadCompute(RS::Albedo);
}
void SVGFTemporalPass::Execute(const SVGFTemporalData& data,
                               ComputeExecutionContext& ctx)
{
    int demod = m_Config.useAlbedoDemod ? 1 : 0;
    ctx.BindPipeline("SVGF_Temporal");
    ctx.PushConstants(VK_SHADER_STAGE_ALL, demod);
    ctx.Dispatch("SVGF_Temporal", (ctx.GetGraph().GetWidth() + 15) / 16,
                 (ctx.GetGraph().GetHeight() + 15) / 16);
}

    // --- Variance Estimate Pass (FilterMoments) ---
SVGFVarianceEstimatePass::SVGFVarianceEstimatePass(
    const SVGFPass::Config& config, const std::string& inputIllum,
    const std::string& inputMoments, const std::string& outputIllum)
    : m_Config(config),
      m_InputIllum(inputIllum),
      m_InputMoments(inputMoments),
      m_OutputIllum(outputIllum)
{
}
void SVGFVarianceEstimatePass::Setup(SVGFVarianceEstimateData& data,
                                     RenderGraph::PassBuilder& builder)
{
    data.inputIllum = builder.ReadCompute(m_InputIllum);
    data.inputMoments = builder.ReadCompute(m_InputMoments);
    data.normal = builder.ReadCompute(RS::Normal);
    data.motion = builder.ReadCompute(RS::Motion);
    data.objectID = builder.ReadCompute(RS::ObjectID);
    data.outputIllum = builder.WriteStorage(m_OutputIllum)
                           .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
}
void SVGFVarianceEstimatePass::Execute(const SVGFVarianceEstimateData& data,
                                       ComputeExecutionContext& ctx)
{
    int demod = m_Config.useAlbedoDemod ? 1 : 0;
    ctx.BindPipeline("SVGF_FilterMoments");
    ctx.PushConstants(VK_SHADER_STAGE_ALL, demod);
    ctx.Dispatch("SVGF_FilterMoments", (ctx.GetGraph().GetWidth() + 15) / 16,
                 (ctx.GetGraph().GetHeight() + 15) / 16);
}

    // --- Atrous Pass ---
SVGFAtrousPass::SVGFAtrousPass(const SVGFPass::Config& config, int iteration,
                               const std::string& inputName,
                               const std::string& outputName,
                               const std::string& historyName)
    : m_Config(config),
      m_Iteration(iteration),
      m_InputName(inputName),
      m_OutputName(outputName),
      m_HistoryName(historyName)
{
}
void SVGFAtrousPass::Setup(SVGFAtrousData& data,
                           RenderGraph::PassBuilder& builder)
{
    data.input = builder.ReadCompute(m_InputName);
    data.normal = builder.ReadCompute(RS::Normal);
    data.depth = builder.ReadCompute(RS::Motion);
    data.objectID = builder.ReadCompute(RS::ObjectID);
    data.materialParams = builder.ReadCompute(RS::MaterialParams);

    auto outputProxy = builder.WriteStorage(m_OutputName)
                           .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    if (!m_HistoryName.empty())
    {
        outputProxy.SaveAsHistory(m_HistoryName);
    }
    data.output = outputProxy;
}
void SVGFAtrousPass::Execute(const SVGFAtrousData& data,
                             ComputeExecutionContext& ctx)
{
    struct PushConstants
    {
        int step;
        int useAlbedoDemod;
    } pc;
    pc.step = 1 << m_Iteration;
    pc.useAlbedoDemod = m_Config.useAlbedoDemod ? 1 : 0;

    ctx.BindPipeline("SVGF_Atrous");
    ctx.PushConstants(VK_SHADER_STAGE_ALL, pc);
    ctx.Dispatch("SVGF_Atrous", (ctx.GetGraph().GetWidth() + 15) / 16,
                 (ctx.GetGraph().GetHeight() + 15) / 16);
}

    // --- Combine Pass ---
SVGFCombinePass::SVGFCombinePass(
    const SVGFPass::Config& config,
    const std::string& currentInputColor)
    : m_Config(config),
      m_CurrentInputColor(currentInputColor)
{
}

void SVGFCombinePass::Setup(SVGFCombineData& data,
                            RenderGraph::PassBuilder& builder)
{
    data.current = builder.ReadCompute(m_CurrentInputColor, "gCurrentFiltered");
    data.output = builder.WriteStorage(m_Config.prefix + "_Filtered_Final")
                      .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                      .BindTo("outFinal");
    data.albedo = builder.ReadCompute(RS::Albedo, "gAlbedo");
}
void SVGFCombinePass::Execute(const SVGFCombineData& data,
                              ComputeExecutionContext& ctx)
{
    int remod = m_Config.useAlbedoDemod ? 1 : 0;
    ctx.BindPipeline("SVGF_Combine");
    ctx.PushConstants(VK_SHADER_STAGE_ALL, remod);
    ctx.Dispatch("SVGF_Combine", (ctx.GetGraph().GetWidth() + 15) / 16,
                 (ctx.GetGraph().GetHeight() + 15) / 16);
}

void SVGFPass::Add(RenderGraph& graph, std::shared_ptr<Scene> scene,
                   const Config& config)
{
    std::string currentInputColor = config.inputName;

    if (config.temporalEnabled)
    {
        graph.AddPass<SVGFTemporalPass>(config);

        std::string tempColor = config.prefix + "_TemporalColor";
        std::string tempMoments = config.prefix + "_TemporalMoments";

        std::string estimateColor = config.prefix + "_EstimatedColor";
        graph.AddPass<SVGFVarianceEstimatePass>(config, tempColor, tempMoments,
                                                estimateColor);

        currentInputColor = estimateColor;
    }

    if (config.spatialEnabled)
    {
        for (int i = 0; i < config.atrousIterations; ++i)
        {
            std::string outputName =
                config.prefix + "_Filtered_" + std::to_string(i);

            if (i == 0)
            {
                graph.AddPass<SVGFAtrousPass>(config, i, currentInputColor,
                                              outputName,
                                              config.historyBaseName);
            }
            else
            {
                graph.AddPass<SVGFAtrousPass>(config, i, currentInputColor,
                                              outputName);
            }
            currentInputColor = outputName;
        }
    }

    if (config.temporalEnabled || config.spatialEnabled)
    {
        graph.AddPass<SVGFCombinePass>(config, currentInputColor);
    }
}
} // namespace Chimera
