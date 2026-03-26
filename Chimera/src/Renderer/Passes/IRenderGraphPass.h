#pragma once
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"

namespace Chimera
{
    /**
     * @brief Base interface for all RenderGraph passes.
     * Allows non-templated access to pass metadata.
     */
    class IRenderPass
    {
    public:
        virtual ~IRenderPass() = default;

        // Future extension: virtual const std::string& GetName() const = 0;
    };

    /**
     * @brief Base class for all RenderGraph passes.
     * Use with RenderGraph::AddPass<MyPass>(args...)
     * 
     * @tparam TData Struct containing RGResourceHandles for the pass
     */
    template<typename TData>
    class RenderPass : public IRenderPass
    {
    public:
        using PassData = TData;

        virtual ~RenderPass() = default;

        /**
         * @brief Define resource dependencies (Read/Write)
         */
        virtual void Setup(PassData& data, RenderGraph::PassBuilder& builder) = 0;

        /**
         * @brief Record Vulkan commands
         */
        virtual void Execute(const PassData& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) = 0;
    };
}
