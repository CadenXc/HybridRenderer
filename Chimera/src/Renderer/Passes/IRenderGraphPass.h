#pragma once
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera
{
    class GraphicsExecutionContext;
    class ComputeExecutionContext;
    class RaytracingExecutionContext;

    /**
     * @brief Base interface for all RenderGraph passes.
     */
    class IRenderPass
    {
    public:
        virtual ~IRenderPass() = default;
    };

    /**
     * @brief Base class for generic RenderGraph passes.
     */
    template<typename TData>
    class RenderPass : public IRenderPass
    {
    public:
        using PassData = TData;
        virtual ~RenderPass() = default;

        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) = 0;
        virtual void Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) = 0;
    };

    /**
     * @brief Specialized class for Graphics passes.
     */
    template<typename TData>
    class GraphicsPass : public IRenderPass
    {
    public:
        using PassData = TData;
        virtual ~GraphicsPass() = default;

        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) = 0;
        virtual void Execute(const PassData& data, GraphicsExecutionContext& ctx) = 0;
    };

    /**
     * @brief Specialized class for Compute passes.
     */
    template<typename TData>
    class ComputePass : public IRenderPass
    {
    public:
        using PassData = TData;
        virtual ~ComputePass() = default;

        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) = 0;
        virtual void Execute(const PassData& data, ComputeExecutionContext& ctx) = 0;
    };

    /**
     * @brief Specialized class for Raytracing passes.
     */
    template<typename TData>
    class RaytracingPass : public IRenderPass
    {
    public:
        using PassData = TData;
        virtual ~RaytracingPass() = default;

        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) = 0;
        virtual void Execute(const PassData& data, RaytracingExecutionContext& ctx) = 0;
    };
}
