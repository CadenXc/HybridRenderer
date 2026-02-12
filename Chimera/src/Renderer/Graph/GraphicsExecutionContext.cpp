#include "pch.h"
#include "GraphicsExecutionContext.h"
#include "RenderGraphCommon.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/RenderState.h"
#include "Core/Application.h"
#include "Scene/Scene.h"
#include <deque>

namespace Chimera
{
    GraphicsExecutionContext::GraphicsExecutionContext(RenderGraph& graph, RenderPass& pass, VkCommandBuffer cmd)
        : m_Graph(graph), m_Pass(pass), m_Cmd(cmd)
    {
    }

    static std::string NormalizeName(std::string name)
    {
        if (name.find("rt") == 0 && name.size() > 2 && isupper(name[2])) name = name.substr(2);
        else if (name.find("g") == 0 && name.size() > 1 && isupper(name[1])) name = name.substr(1);
        else if (name.find("tex") == 0) name = name.substr(3);
        return name;
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
            for (auto* s : shaders) {
                if (!s) continue;
                auto bindings = s->GetSetBindings(2);
                for (auto& b : bindings) reflection[b.binding] = b;
            }

            if (!reflection.empty()) {
                m_Pass.descriptorSetLayout = PipelineManager::Get().GetSet2Layout(shaders);
                if (m_Pass.descriptorSetLayout == VK_NULL_HANDLE) return;

                VkDescriptorSetAllocateInfo alloc{};
                alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                alloc.descriptorPool = ResourceManager::Get().GetTransientDescriptorPool();
                alloc.descriptorSetCount = 1;
                alloc.pSetLayouts = &m_Pass.descriptorSetLayout;
                if (vkAllocateDescriptorSets(VulkanContext::Get().GetDevice(), &alloc, &m_Pass.descriptorSet) != VK_SUCCESS) return;

                std::vector<VkWriteDescriptorSet> writes;
                std::deque<VkDescriptorImageInfo> imageInfos;

                for (auto& [binding, res] : reflection) {
                    std::string targetName = NormalizeName(res.name);
                    if (!m_Graph.ContainsImage(targetName)) {
                        if (m_Graph.ContainsImage(res.name)) targetName = res.name;
                        else continue;
                    }

                    const GraphImage& phys = m_Graph.GetImage(targetName);
                    
                    VkDescriptorImageInfo info{};
                    info.sampler = ResourceManager::Get().GetDefaultSampler();
                    info.imageView = (phys.debug_view != VK_NULL_HANDLE) ? phys.debug_view : phys.view;
                    info.imageLayout = (res.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
                    imageInfos.push_back(info);

                    VkWriteDescriptorSet w{};
                    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    w.dstSet = m_Pass.descriptorSet;
                    w.dstBinding = binding;
                    w.descriptorCount = 1;
                    w.descriptorType = res.type;
                    w.pImageInfo = &imageInfos.back();
                    writes.push_back(w);
                }
                if (!writes.empty()) vkUpdateDescriptorSets(VulkanContext::Get().GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
            }
        }

        if (m_Pass.descriptorSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(m_Cmd, bindPoint, layout, 2, 1, &m_Pass.descriptorSet, 0, nullptr);
        }
    }

    void GraphicsExecutionContext::DrawMeshes(const GraphicsPipelineDescription& desc, Scene* scene)
    {
        auto& pipe = PipelineManager::Get().GetGraphicsPipeline(m_Pass.colorFormats, m_Pass.depthFormat, desc);
        BindPipelineAndDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.handle, pipe.layout, pipe.shaders);
        
        // [FIX] 空场景安全检查
        if (scene) scene->RenderMeshes(*this);
        // 如果没有场景，则绘制 3 个顶点（全屏大三角形）
        else vkCmdDraw(m_Cmd, 3, 1, 0, 0);
    }

    void GraphicsExecutionContext::DispatchRays(const RaytracingPipelineDescription& desc)
    {
        auto& pipe = PipelineManager::Get().GetRaytracingPipeline(desc);
        BindPipelineAndDescriptorSets(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipe.handle, pipe.layout, pipe.shaders);
        vkCmdTraceRaysKHR(m_Cmd, &pipe.sbt.raygen, &pipe.sbt.miss, &pipe.sbt.hit, &pipe.sbt.callable, m_Graph.GetWidth(), m_Graph.GetHeight(), 1);
    }
}