#pragma once

#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Backend/VulkanCommon.h"
#include "Renderer/Graph/ComputeExecutionContext.h"

namespace Chimera {

    struct BloomPushConstants {
        int mode; // 0: Extract, 1: H-Blur, 2: V-Blur, 3: Composite
        float threshold;
        float intensity;
    };

    class BloomPass {
    public:
        static void Setup(RenderGraph& graph, const std::string& inputName, float threshold, float intensity) {
            uint32_t width = graph.GetWidth();
            uint32_t height = graph.GetHeight();
            uint32_t groupX = (width + 15) / 16;
            uint32_t groupY = (height + 15) / 16;

            ComputePipelineDescription bloomDesc;
            ComputeKernel kernel;
            kernel.shader = "bloom.comp";
            bloomDesc.kernels.push_back(kernel);
            
            bloomDesc.push_constant_description.size = sizeof(BloomPushConstants);
            bloomDesc.push_constant_description.shader_stage = VK_SHADER_STAGE_COMPUTE_BIT;

            // 1. Extract Pass
            graph.AddComputePass("Bloom: Extract",
                { TransientResource::Image(inputName, VK_FORMAT_R16G16B16A16_SFLOAT, 1, {0}, TransientImageType::StorageImage) },
                { TransientResource::Image(RS::BLOOM_BRIGHT, VK_FORMAT_R16G16B16A16_SFLOAT, 1, {0}, TransientImageType::StorageImage) },
                bloomDesc,
                [=](ComputeExecutionContext& ctx) {
                    BloomPushConstants pc{ 0, threshold, intensity };
                    ctx.Dispatch("main", groupX, groupY, 1, pc);
                }, "Bloom_Standard"
            );

            // 2. Horizontal Blur
            graph.AddComputePass("Bloom: Blur H",
                { TransientResource::Image(RS::BLOOM_BRIGHT, VK_FORMAT_R16G16B16A16_SFLOAT, 1, {0}, TransientImageType::StorageImage) },
                { TransientResource::Image(RS::BLOOM_BLUR_TMP, VK_FORMAT_R16G16B16A16_SFLOAT, 1, {0}, TransientImageType::StorageImage) },
                bloomDesc,
                [=](ComputeExecutionContext& ctx) {
                    BloomPushConstants pc{ 1, threshold, intensity };
                    ctx.Dispatch("main", groupX, groupY, 1, pc);
                }, "Bloom_Standard"
            );

            // 3. Vertical Blur
            graph.AddComputePass("Bloom: Blur V",
                { TransientResource::Image(RS::BLOOM_BLUR_TMP, VK_FORMAT_R16G16B16A16_SFLOAT, 1, {0}, TransientImageType::StorageImage) },
                { TransientResource::Image(RS::BLOOM_FINAL, VK_FORMAT_R16G16B16A16_SFLOAT, 1, {0}, TransientImageType::StorageImage) },
                bloomDesc,
                [=](ComputeExecutionContext& ctx) {
                    BloomPushConstants pc{ 2, threshold, intensity };
                    ctx.Dispatch("main", groupX, groupY, 1, pc);
                }, "Bloom_Standard"
            );

            // 4. Composite (Add to input image)
            graph.AddComputePass("Bloom: Composite",
                { TransientResource::Image(RS::BLOOM_FINAL, VK_FORMAT_R16G16B16A16_SFLOAT, 1, {0}, TransientImageType::StorageImage) },
                { TransientResource::Image(inputName, VK_FORMAT_R16G16B16A16_SFLOAT, 1, {0}, TransientImageType::StorageImage) },
                bloomDesc,
                [=](ComputeExecutionContext& ctx) {
                    BloomPushConstants pc{ 3, threshold, intensity };
                    ctx.Dispatch("main", groupX, groupY, 1, pc);
                }, "Bloom_Standard"
            );
        }
    };

}
