#include "pch.h"
#include "Renderer/Graph/RenderGraph.h"
#include "GraphicsExecutionContext.h"
#include "RenderGraphCommon.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/Shader.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/RenderState.h"
#include "Core/Application.h"
#include "Scene/Scene.h"
#include <deque>

namespace Chimera
{
    GraphicsExecutionContext::GraphicsExecutionContext(RenderGraph& graph, struct RenderPass& pass, VkCommandBuffer cmd)
        : m_Graph(graph), m_Pass(pass), m_Cmd(cmd) 
    {
    }

    void GraphicsExecutionContext::BindPipelineAndDescriptorSets(VkPipelineBindPoint bindPoint, VkPipeline handle, VkPipelineLayout layout, const std::vector<const Shader*>& shaders) 
    {
        m_ActiveLayout = layout; 
        vkCmdBindPipeline(m_Cmd, bindPoint, handle);

        uint32_t fIdx = Application::Get().GetTotalFrameCount() % MAX_FRAMES_IN_FLIGHT;
        VkDescriptorSet globals[] = { 
            Application::Get().GetRenderState()->GetDescriptorSet(fIdx), 
            ResourceManager::Get().GetSceneDescriptorSet(fIdx) 
        };
        vkCmdBindDescriptorSets(m_Cmd, bindPoint, layout, 0, 2, globals, 0, nullptr);

        if (m_Pass.descriptorSet == VK_NULL_HANDLE) 
        {
            std::map<uint32_t, ShaderResource> reflection;
            for (auto* s : shaders) 
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
                m_Pass.descriptorSetLayout = PipelineManager::Get().GetSet2Layout(shaders);
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
                        
                        // --- FIX: Use the actual physical layout tracked for this specific VkImage handle ---
                        info.imageLayout = m_Graph.m_PhysicalImageStates[rgRes.image.handle].layout;
                        
                        // Safety fallback for first-use or undefined states
                        if (info.imageLayout == VK_IMAGE_LAYOUT_UNDEFINED) 
                        {
                            info.imageLayout = (res.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
            vkCmdBindDescriptorSets(m_Cmd, bindPoint, layout, 2, 1, &m_Pass.descriptorSet, 0, nullptr);
        }
    }

    void GraphicsExecutionContext::BindPipeline(const GraphicsPipelineDescription& desc) 
    {
        auto& pipe = PipelineManager::Get().GetGraphicsPipeline(m_Pass.colorFormats, m_Pass.depthFormat, desc);
        
        // --- Record Shader Names [NEW] ---
        for (auto* s : pipe.shaders)
        {
            if (s)
            {
                bool alreadyAdded = false;
                for (const auto& existing : m_Pass.shaderNames) if (existing == s->GetName()) alreadyAdded = true;
                if (!alreadyAdded) m_Pass.shaderNames.push_back(s->GetName());
            }
        }

        BindPipelineAndDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.handle, pipe.layout, pipe.shaders);
    }

    void GraphicsExecutionContext::DrawMeshes(const GraphicsPipelineDescription& desc, Scene* scene) 
    {
        BindPipeline(desc);
        if (scene) 
        {
            scene->RenderMeshes(*this);
        }
        else 
        {
            vkCmdDraw(m_Cmd, 3, 1, 0, 0);
        }
    }
}
