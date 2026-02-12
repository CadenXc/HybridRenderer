#include "pch.h"
#include "ComputeExecutionContext.h"
#include "RenderGraphCommon.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/RenderState.h"
#include "Core/Application.h"

namespace Chimera
{
    ComputeExecutionContext::ComputeExecutionContext(RenderGraph& graph, RenderPass& pass, VkCommandBuffer cmd)
        : m_Graph(graph), m_Pass(pass), m_Cmd(cmd)
    {
    }

    static std::string NormalizeName(std::string name)
    {
        if (name.find("rt") == 0) name = name.substr(2);
        else if (name.find("g") == 0 && name.size() > 1 && isupper(name[1])) name = name.substr(1);
        else if (name.find("tex") == 0) name = name.substr(3);
        return name;
    }

    void ComputeExecutionContext::BindAutomaticSets(VkPipelineBindPoint bindPoint, const std::vector<const Shader*>& shaders, VkPipelineLayout layout)
    {
        uint32_t frameIdx = Application::Get().GetTotalFrameCount() % MAX_FRAMES_IN_FLIGHT;
        VkDescriptorSet globalSets[] = { 
            Application::Get().GetRenderState()->GetDescriptorSet(frameIdx), 
            ResourceManager::Get().GetSceneDescriptorSet(frameIdx) 
        };
        
        // 1. 绑定 Global 和 Scene (Set 0 & 1)
        vkCmdBindDescriptorSets(m_Cmd, bindPoint, layout, 0, 2, globalSets, 0, nullptr);

        // 2. [AUTOMATED] 动态生成并绑定 Set 2 (Pass-specific)
        if (m_Pass.descriptorSet == VK_NULL_HANDLE)
        {
            std::map<uint32_t, ShaderResource> reflection;
            for (auto* s : shaders)
            {
                if (!s) continue;
                auto bindings = s->GetSetBindings(2);
                for (auto& b : bindings) reflection[b.binding] = b;
            }

            if (reflection.empty()) return;

            // 获取 Layout
            m_Pass.descriptorSetLayout = PipelineManager::Get().GetSet2Layout(shaders);
            if (m_Pass.descriptorSetLayout == VK_NULL_HANDLE) return;

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = ResourceManager::Get().GetTransientDescriptorPool();
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &m_Pass.descriptorSetLayout;

            if (vkAllocateDescriptorSets(VulkanContext::Get().GetDevice(), &allocInfo, &m_Pass.descriptorSet) != VK_SUCCESS) return;

            std::vector<VkWriteDescriptorSet> writes;
            std::vector<VkDescriptorImageInfo> imageInfos;
            imageInfos.reserve(reflection.size());

            for (auto& [binding, res] : reflection)
            {
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

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = m_Pass.descriptorSet;
                write.dstBinding = binding;
                write.descriptorCount = 1;
                write.descriptorType = res.type;
                write.pImageInfo = &imageInfos.back();
                writes.push_back(write);
            }
            if (!writes.empty()) vkUpdateDescriptorSets(VulkanContext::Get().GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
        }

        if (m_Pass.descriptorSet != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(m_Cmd, bindPoint, layout, 2, 1, &m_Pass.descriptorSet, 0, nullptr);
        }
    }

    void ComputeExecutionContext::DispatchRays(const RaytracingPipelineDescription& desc)
    {
        auto& pipe = PipelineManager::Get().GetRaytracingPipeline(desc);
        vkCmdBindPipeline(m_Cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipe.handle);
        
        // [FIX] 使用统一的 shaders 成员进行反射对齐
        BindAutomaticSets(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipe.shaders, pipe.layout);

        vkCmdTraceRaysKHR(m_Cmd, &pipe.sbt.raygen, &pipe.sbt.miss, &pipe.sbt.hit, &pipe.sbt.callable, m_Graph.GetWidth(), m_Graph.GetHeight(), 1);
    }

    void ComputeExecutionContext::Dispatch(const std::string& shaderName, uint32_t groupX, uint32_t groupY, uint32_t groupZ)
    {
        ComputePipelineDescription::Kernel k{};
        k.name = shaderName; k.shader = shaderName + ".comp";
        auto& pipe = PipelineManager::Get().GetComputePipeline(k);

        vkCmdBindPipeline(m_Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe.handle);
        // [FIX] 使用统一的 shaders 成员进行反射对齐
        BindAutomaticSets(VK_PIPELINE_BIND_POINT_COMPUTE, pipe.shaders, pipe.layout);

        vkCmdDispatch(m_Cmd, groupX, groupY, groupZ);
    }
}