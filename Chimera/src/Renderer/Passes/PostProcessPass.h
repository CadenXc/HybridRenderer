#pragma once

#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Graph/RenderGraph.h"
#include "IRenderGraphPass.h"
#include <string>
#include <memory>

namespace Chimera
{
struct PostProcessPassData
{
    RGResourceHandle input;
    RGResourceHandle output;
};

class PostProcessPass : public RenderPass<PostProcessPassData>
{
public:
    using PassData = PostProcessPassData;
    static constexpr const char* Name = "PostProcessPass";

    using Data = PassData;

    PostProcessPass(const std::string& inputName = "TAAOutput");

    virtual void Setup(PassData& data,
                       RenderGraph::PassBuilder& builder) override;
    virtual void Execute(const PassData& data, RenderGraphRegistry& reg,
                         VkCommandBuffer cmd) override;

private:
    std::string m_InputName;
};
} // namespace Chimera
