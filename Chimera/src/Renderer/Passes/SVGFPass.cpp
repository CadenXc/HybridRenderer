#include "pch.h"
#include "SVGFPass.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ComputeExecutionContext.h"

namespace Chimera
{
    // --- Temporal Pass ---
    SVGFTemporalPass::SVGFTemporalPass(const SVGFPass::Config& config) : m_Config(config) {}
    void SVGFTemporalPass::Setup(SVGFTemporalData& data, RenderGraph::PassBuilder& builder)
    {
        data.cur            = builder.ReadCompute(m_Config.inputName);        
        data.motion         = builder.ReadCompute(RS::Motion);             
        data.history        = builder.ReadHistory(m_Config.historyBaseName); 
        data.historyMoments = builder.ReadHistory(m_Config.prefix + "Moments"); 
        
        data.output         = builder.WriteStorage(m_Config.prefix + "_TemporalColor").Format(VK_FORMAT_R16G16B16A16_SFLOAT).SaveAsHistory(m_Config.historyBaseName); 
        data.outMoments     = builder.WriteStorage(m_Config.prefix + "_TemporalMoments").Format(VK_FORMAT_R16G16B16A16_SFLOAT).SaveAsHistory(m_Config.prefix + "Moments"); 
        
        data.depth          = builder.ReadCompute(RS::Depth);              
        data.normal         = builder.ReadCompute(RS::Normal);             
        data.prevDepth      = builder.ReadHistory(RS::Depth);              
        data.prevNormal     = builder.ReadHistory(RS::Normal); 
    }
    void SVGFTemporalPass::Execute(const SVGFTemporalData& data, ComputeExecutionContext& ctx)
    {
        ctx.BindPipeline("SVGF_Temporal");
        ctx.Dispatch("SVGF_Temporal", (ctx.GetGraph().GetWidth() + 15) / 16, (ctx.GetGraph().GetHeight() + 15) / 16);
    }

    // --- Atrous Pass ---
    SVGFAtrousPass::SVGFAtrousPass(const SVGFPass::Config& config, int iteration, const std::string& inputName, const std::string& outputName, const std::string& momentsName)
        : m_Config(config), m_Iteration(iteration), m_InputName(inputName), m_OutputName(outputName), m_MomentsName(momentsName) {}
    void SVGFAtrousPass::Setup(SVGFAtrousData& data, RenderGraph::PassBuilder& builder)
    {
        data.input   = builder.ReadCompute(m_InputName);   
        data.moments = builder.ReadCompute(m_MomentsName); 
        data.normal  = builder.ReadCompute(RS::Normal);          
        data.depth   = builder.ReadCompute(RS::Depth);           
        data.output  = builder.WriteStorage(m_OutputName).Format(VK_FORMAT_R16G16B16A16_SFLOAT); 
    }
    void SVGFAtrousPass::Execute(const SVGFAtrousData& data, ComputeExecutionContext& ctx)
    {
        int step = 1 << m_Iteration;
        ctx.BindPipeline("SVGF_Atrous");
        ctx.PushConstants(VK_SHADER_STAGE_ALL, step);
        ctx.Dispatch("SVGF_Atrous", (ctx.GetGraph().GetWidth() + 15) / 16, (ctx.GetGraph().GetHeight() + 15) / 16);
    }

    // --- Combine Pass ---
    SVGFCombinePass::SVGFCombinePass(const SVGFPass::Config& config, const std::string& currentInputColor, const std::string& temporalMomentsName)
        : m_Config(config), m_CurrentInputColor(currentInputColor), m_TemporalMomentsName(temporalMomentsName) {}
    void SVGFCombinePass::Setup(SVGFCombineData& data, RenderGraph::PassBuilder& builder)
    {
        data.current = builder.ReadCompute(m_CurrentInputColor); 
        data.history = builder.ReadHistory(m_Config.historyBaseName);
        data.moments = builder.ReadCompute(m_TemporalMomentsName);
        data.output  = builder.WriteStorage(m_Config.prefix + "_Filtered_Final").Format(VK_FORMAT_R16G16B16A16_SFLOAT).SaveAsHistory(m_Config.historyBaseName);
    }
    void SVGFCombinePass::Execute(const SVGFCombineData& data, ComputeExecutionContext& ctx)
    {
        ctx.BindPipeline("SVGF_Combine");
        ctx.Dispatch("SVGF_Combine", (ctx.GetGraph().GetWidth() + 15) / 16, (ctx.GetGraph().GetHeight() + 15) / 16);
    }

    // --- High-level Factory ---
    void SVGFPass::Add(RenderGraph& graph, std::shared_ptr<Scene> scene, const Config& config)
    {
        std::string currentInputColor = config.inputName;
        std::string currentInputMoments = "";

        // 1. Temporal
        if (config.temporalEnabled)
        {
            graph.AddPass<SVGFTemporalPass>(config);
            currentInputColor = config.prefix + "_TemporalColor";
            currentInputMoments = config.prefix + "_TemporalMoments";
        }

        // 2. Atrous
        if (config.spatialEnabled)
        {
            // If temporal is off, we still need a moments buffer for the Atrous shader's binding,
            // though variance will be 0 or uninitialized if we didn't run temporal.
            // For now, assume if spatial is on but temporal is off, it just filters the raw input.
            if (currentInputMoments.empty())
            {
                // Create a dummy moments buffer if needed, but usually we expect them to be used together.
                currentInputMoments = config.prefix + "_TemporalMoments"; 
            }

            for (int i = 0; i < config.atrousIterations; ++i)
            {
                std::string outputName = config.prefix + "_Filtered_" + std::to_string(i);
                graph.AddPass<SVGFAtrousPass>(config, i, currentInputColor, outputName, currentInputMoments);
                currentInputColor = outputName;
            }
        }

        // 3. Combine
        // The final filtered output is aliased to config.prefix + "_Filtered_Final" to satisfy dependencies.
        // We use a simple CopyPass or similar if we want to skip Combine when no filtering happened, 
        // but SVGFPass::Add is only called if SVGF is "active" at some level.
        
        if (config.temporalEnabled || config.spatialEnabled)
        {
            graph.AddPass<SVGFCombinePass>(config, currentInputColor, config.prefix + "_TemporalMoments");
        }
        else
        {
            // If neither is enabled but we were asked to add SVGF, just provide the raw input as the "final"
            // This case shouldn't really happen with the current BuildGraph logic.
        }
    }
}
