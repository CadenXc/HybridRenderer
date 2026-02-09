#pragma once
#include "Renderer/Graph/RenderGraph.h"
#include "ComputeExecutionContext.h"

namespace Chimera {

    class BloomPass {
    public:
        BloomPass(uint32_t width, uint32_t height) : m_Width(width), m_Height(height) {}

        void Setup(RenderGraph& graph) {
            graph.AddComputePass({
                .Name = "BloomBrightPass",
                .Dependencies = { TransientResource::Image(RS::FinalColor, VK_FORMAT_B8G8R8A8_UNORM) },
                .Outputs = { TransientResource::StorageImage(RS::AtrousPing, VK_FORMAT_R16G16B16A16_SFLOAT) }, // Re-using for bloom
                .Pipeline = { .kernels = { { "Bright", "bloom.comp" } } },
                .Callback = [this](ComputeExecutionContext& ctx) {
                    ctx.Bind("Bright");
                    ctx.Dispatch(m_Width / 8, m_Height / 8, 1);
                },
                .ShaderLayout = "BloomLayout"
            });
        }

    private:
        uint32_t m_Width, m_Height;
    };

}