#pragma once
#include "Renderer/Graph/core/RenderGraph.h"

namespace Chimera {

	class IRenderGraphPass
	{
	public:
		virtual ~IRenderGraphPass() = default;

		// Add this Pass logic (Resource definition, Pipeline, Callbacks) to the Graph.
		virtual void AddToGraph(RenderGraph& graph, uint32_t width, uint32_t height) = 0;
	};

}
