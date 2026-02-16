#include "pch.h"
#include "RaytracingExecutionContext.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Core/Application.h"
#include "Renderer/RenderState.h"

namespace Chimera
{
    RaytracingExecutionContext::RaytracingExecutionContext(RenderGraph& graph, RenderPass& pass, VkCommandBuffer cmd)
        : m_Graph(graph), m_Pass(pass), m_Cmd(cmd) 
    {
    }

    static RaytracingPipeline* s_ActiveRTPipe = nullptr;

    void RaytracingExecutionContext::BindPipeline(const RaytracingPipelineDescription& desc)
    {
        auto& pipe = PipelineManager::Get().GetRaytracingPipeline(desc);
        m_ActiveLayout = pipe.layout;
        vkCmdBindPipeline(m_Cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipe.handle);

        uint32_t fIdx = Application::Get().GetTotalFrameCount() % MAX_FRAMES_IN_FLIGHT;
        VkDescriptorSet globals[] = { 
            Application::Get().GetRenderState()->GetDescriptorSet(fIdx), 
            ResourceManager::Get().GetSceneDescriptorSet(fIdx) 
        };
        vkCmdBindDescriptorSets(m_Cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipe.layout, 0, 2, globals, 0, nullptr);

        if (m_Pass.descriptorSet == VK_NULL_HANDLE)
        {
            std::map<uint32_t, ShaderResource> reflection;
            for (auto* s : pipe.shaders)
            {
                if (!s)
                {
                    continue;
                }
                auto bindings = s->GetSetBindings(2);
                for (auto& b : bindings)
                {
                    reflection[b.binding] = b;
                }
            }

            if (!reflection.empty()) 
            {
                m_Pass.descriptorSetLayout = PipelineManager::Get().GetSet2Layout(pipe.shaders);
                VkDescriptorSetAllocateInfo alloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
                alloc.descriptorPool = ResourceManager::Get().GetTransientDescriptorPool();
                alloc.descriptorSetCount = 1;
                alloc.pSetLayouts = &m_Pass.descriptorSetLayout;
                vkAllocateDescriptorSets(VulkanContext::Get().GetDevice(), &alloc, &m_Pass.descriptorSet);

                std::vector<VkWriteDescriptorSet> writes;
                std::deque<VkDescriptorImageInfo> imageInfos;

                uint32_t inputIdx = 0; uint32_t outputIdx = 0;

                for (auto& [binding, res] : reflection) 
                {
                    RGResourceHandle targetHandle = INVALID_RESOURCE;
                    if (res.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) 
                    {
                        if (inputIdx < m_Pass.inputs.size()) 
                        {
                            targetHandle = m_Pass.inputs[inputIdx++].handle;
                        }
                    } 
                    else if (res.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) 
                    {
                        if (outputIdx < m_Pass.outputs.size()) 
                        {
                            targetHandle = m_Pass.outputs[outputIdx++].handle;
                        }
                    }

                    VkDescriptorImageInfo info{};
                    info.sampler = ResourceManager::Get().GetDefaultSampler();
                    if (targetHandle != INVALID_RESOURCE) 
                    {
                        auto& rgRes = m_Graph.m_Resources[targetHandle];
                        info.imageView = (rgRes.image.debug_view != VK_NULL_HANDLE) ? rgRes.image.debug_view : rgRes.image.view;
                        
                        // --- FIX: Respect the layout tracked by RenderGraph. Only override if UNDEFINED. ---
                        if (res.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                        {
                            info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                        }
                        else
                        {
                            // --- FIX: Use the actual physical layout tracked for this specific VkImage handle ---
                            info.imageLayout = m_Graph.m_PhysicalImageStates[rgRes.image.handle].layout;
                            
                            if (info.imageLayout == VK_IMAGE_LAYOUT_UNDEFINED) 
                            {
                                info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                            }
                        }
                    }

                    if (info.imageView == VK_NULL_HANDLE) 
                    {
                        info.imageView = ResourceManager::Get().GetBlackTexture().GetImageView();
                        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    }

                    imageInfos.push_back(info);
                    VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                    w.dstSet = m_Pass.descriptorSet; w.dstBinding = binding; w.descriptorCount = 1; w.descriptorType = res.type;
                    w.pImageInfo = &imageInfos.back();
                    writes.push_back(w);
                }
                if (!writes.empty()) 
                {
                    vkUpdateDescriptorSets(VulkanContext::Get().GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
                }
            }
        }

        if (m_Pass.descriptorSet != VK_NULL_HANDLE) 
        {
            vkCmdBindDescriptorSets(m_Cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipe.layout, 2, 1, &m_Pass.descriptorSet, 0, nullptr);
        }
        s_ActiveRTPipe = &pipe;
    }

    void RaytracingExecutionContext::TraceRays(uint32_t width, uint32_t height, uint32_t depth)
    {
        if (s_ActiveRTPipe) 
        {
            vkCmdTraceRaysKHR(m_Cmd, &s_ActiveRTPipe->sbt.raygen, &s_ActiveRTPipe->sbt.miss, &s_ActiveRTPipe->sbt.hit, &s_ActiveRTPipe->sbt.callable, width, height, depth);
        }
    }
}
