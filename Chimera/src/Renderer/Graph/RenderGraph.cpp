#include "pch.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/ShaderMetadata.h"
#include "Renderer/RenderState.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Backend/Renderer.h"
#include "Core/Application.h"
#include "Scene/Scene.h"
#include <deque>
#include <imgui.h>

namespace Chimera
{
    // ---------------------------------------------------------------------------------------------------------------------
    // [ExecutionContext Implementations]
    // ---------------------------------------------------------------------------------------------------------------------

    void RaytracingExecutionContext::Dispatch(uint32_t w, uint32_t h, uint32_t d)
    {
        auto& pipe = m_Pipe;
        if (pipe.handle == VK_NULL_HANDLE)
        {
            return;
        }
        vkCmdBindPipeline(m_Cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipe.handle);
        vkCmdTraceRaysKHR(m_Cmd, &pipe.sbt.raygen, &pipe.sbt.miss, &pipe.sbt.hit, &pipe.sbt.callable, w, h, d);
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // [RenderGraph Lifecycle]
    // ---------------------------------------------------------------------------------------------------------------------

    RenderGraph::RenderGraph(VulkanContext& ctx, uint32_t w, uint32_t h)
        : m_Context(ctx), m_Width(w), m_Height(h)
    {
        VkQueryPoolCreateInfo queryPoolInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = 128;
        vkCreateQueryPool(m_Context.GetDevice(), &queryPoolInfo, nullptr, &m_TimestampQueryPool);
    }

    RenderGraph::~RenderGraph()
    {
        DestroyResources(true);
        if (m_TimestampQueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(m_Context.GetDevice(), m_TimestampQueryPool, nullptr);
        }
    }

    void RenderGraph::DestroyResources(bool all)
    {
        VkDevice device = m_Context.GetDevice();
        
        // 1. Cleanup Pass-Specific Descriptors
        for (auto& [name, pass] : m_Passes)
        {
            if (pass.descriptor_set_layout != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, pass.descriptor_set_layout, nullptr);
            }
        }
        m_Passes.clear();
        m_PassDescriptions.clear();
        m_ExecutionOrder.clear();

        // 2. Cleanup Graph-Owned Images
        for (auto& [name, img] : m_Images)
        {
            if (!img.is_external || all)
            {
                ResourceManager::Get().DestroyGraphImage(img);
            }
        }
        m_Images.clear();
        m_ImageAccess.clear();

        // 3. Clear Pipeline Pointers
        m_GraphicsPipelines.clear();
        m_RaytracingPipelines.clear();
        m_ComputePipelines.clear();
        m_SamplerArrays.clear();
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // [Graph Construction API]
    // ---------------------------------------------------------------------------------------------------------------------

    void RenderGraph::AddGraphicsPass(const GraphicsPassSpecification& s)
    {
        RenderPassDescription d;
        d.name = s.Name;
        d.dependencies = s.Dependencies;
        d.outputs = s.Outputs;
        d.description = s.Pipelines[0];
        d.callback = s.Callback;
        m_PassDescriptions[s.Name] = d;
        m_ExecutionOrder.push_back(s.Name);
    }

    void RenderGraph::AddRaytracingPass(const RaytracingPassSpecification& s)
    {
        RenderPassDescription d;
        d.name = s.Name;
        d.dependencies = s.Dependencies;
        d.outputs = s.Outputs;
        d.description = s.Pipeline;
        d.callback = s.Callback;
        m_PassDescriptions[s.Name] = d;
        m_ExecutionOrder.push_back(s.Name);
    }

    void RenderGraph::AddComputePass(const ComputePassSpecification& s)
    {
        RenderPassDescription d;
        d.name = s.Name;
        d.dependencies = s.Dependencies;
        d.outputs = s.Outputs;
        d.description = s.Pipeline;
        d.callback = s.Callback;
        m_PassDescriptions[s.Name] = d;
        m_ExecutionOrder.push_back(s.Name);
    }

    void RenderGraph::AddBlitPass(const std::string& n, const std::string& s, const std::string& d, VkFormat sf, VkFormat df)
    {
        RenderPassDescription desc;
        desc.name = n;
        desc.description = BlitPassDescription{};
        desc.dependencies.push_back(TransientResource::Image(s, sf));
        desc.outputs.push_back(TransientResource::Image(d, df));
        m_PassDescriptions[n] = desc;
        m_ExecutionOrder.push_back(n);
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // [Compilation Logic]
    // ---------------------------------------------------------------------------------------------------------------------

    void RenderGraph::Build()
    {
        CH_CORE_INFO("RenderGraph: Building Graph ({0}x{1})...", m_Width, m_Height);

        // Phase 1: Resource Virtualization & Creation
        for (auto& passName : m_ExecutionOrder)
        {
            auto& desc = m_PassDescriptions.at(passName);
            
            auto processResource = [&](const TransientResource& res)
            {
                if (res.type == TransientResourceType::Image && res.name != RS::RENDER_OUTPUT && !m_Images.count(res.name))
                {
                    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                    
                    if (VulkanUtils::IsDepthFormat(res.image.format))
                    {
                        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                    }
                    else
                    {
                        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
                    }

                    m_Images[res.name] = ResourceManager::Get().CreateGraphImage(m_Width, m_Height, res.image.format, usage, VK_IMAGE_LAYOUT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);
                    m_ImageAccess[res.name] = { VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
                    m_Context.SetDebugName((uint64_t)m_Images[res.name].handle, VK_OBJECT_TYPE_IMAGE, res.name.c_str());
                }
            };

            for (auto& res : desc.dependencies) processResource(res);
            for (auto& res : desc.outputs) processResource(res);
        }

        // Phase 2: Pipeline Creation & Pass Linking
        for (auto& passName : m_ExecutionOrder)
        {
            auto& desc = m_PassDescriptions.at(passName);
            if (std::holds_alternative<GraphicsPipelineDescription>(desc.description))
            {
                CreateGraphicsPass(desc);
            }
            else if (std::holds_alternative<RaytracingPipelineDescription>(desc.description))
            {
                CreateRaytracingPass(desc);
            }
            else if (std::holds_alternative<ComputePipelineDescription>(desc.description))
            {
                CreateComputePass(desc);
            }
            else if (std::holds_alternative<BlitPassDescription>(desc.description))
            {
                m_Passes[passName] = { passName, VK_NULL_HANDLE, VK_NULL_HANDLE, std::make_shared<RenderPass::PrivateInfo>(), BlitPass{desc.dependencies[0].name, desc.outputs[0].name} };
            }
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // [Pass Creation Subroutines]
    // ---------------------------------------------------------------------------------------------------------------------

    void RenderGraph::CreateGraphicsPass(RenderPassDescription& d)
    {
        GraphicsPass gp;
        gp.callback = std::get<GraphicsPassCallback>(d.callback);
        gp.attachments = d.outputs;
        RenderPass pass { d.name, VK_NULL_HANDLE, VK_NULL_HANDLE, std::make_shared<RenderPass::PrivateInfo>(), gp };
        
        // Setup pass-specific descriptors (Set 2)
        auto pInfo = pass.private_info;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        
        for (auto& dep : d.dependencies)
        {
            if (dep.type == TransientResourceType::Image && m_Images.count(dep.name))
            {
                auto& img = m_Images.at(dep.name);
                uint32_t b = (dep.image.binding != 0xFFFFFFFF) ? dep.image.binding : (uint32_t)bindings.size();
                pInfo->images.push_back({ ResourceManager::Get().GetDefaultSampler(), img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
                bindings.push_back({ b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr });
            }
        }

        if (!bindings.empty())
        {
            VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data() };
            vkCreateDescriptorSetLayout(m_Context.GetDevice(), &layoutInfo, nullptr, &pass.descriptor_set_layout);
            
            VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, ResourceManager::Get().GetTransientDescriptorPool(), 1, &pass.descriptor_set_layout };
            vkAllocateDescriptorSets(m_Context.GetDevice(), &allocInfo, &pass.descriptor_set);
            m_Context.SetDebugName((uint64_t)pass.descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, (d.name + "_Set2_Pass").c_str());
            
            std::vector<VkWriteDescriptorSet> writes;
            for (uint32_t i = 0; i < (uint32_t)bindings.size(); ++i)
            {
                writes.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, pass.descriptor_set, bindings[i].binding, 0, 1, bindings[i].descriptorType, &pInfo->images[i], nullptr, nullptr });
            }
            vkUpdateDescriptorSets(m_Context.GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
        }

        m_Passes[d.name] = pass;
        m_GraphicsPipelines[d.name] = &PipelineManager::Get().GetGraphicsPipeline(m_Passes[d.name], std::get<GraphicsPipelineDescription>(d.description));
    }

    void RenderGraph::CreateRaytracingPass(RenderPassDescription& d)
    {
        RaytracingPass rp;
        rp.callback = std::get<RaytracingPassCallback>(d.callback);
        RenderPass pass { d.name, VK_NULL_HANDLE, VK_NULL_HANDLE, std::make_shared<RenderPass::PrivateInfo>(), rp };
        
        auto pInfo = pass.private_info;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        
        auto collectBinding = [&](const TransientResource& res)
        {
            if (res.type == TransientResourceType::Image && m_Images.count(res.name))
            {
                auto& img = m_Images.at(res.name);
                uint32_t b = (res.image.binding != 0xFFFFFFFF) ? res.image.binding : (uint32_t)bindings.size();
                VkImageLayout l = (res.image.type == TransientImageType::StorageImage) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                pInfo->images.push_back({ ResourceManager::Get().GetDefaultSampler(), img.view, l });
                bindings.push_back({ b, (res.image.type == TransientImageType::StorageImage) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr });
            }
        };

        for (auto& dep : d.dependencies) collectBinding(dep);
        for (auto& out : d.outputs) collectBinding(out);

        if (!bindings.empty())
        {
            VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data() };
            vkCreateDescriptorSetLayout(m_Context.GetDevice(), &layoutInfo, nullptr, &pass.descriptor_set_layout);
            
            VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, ResourceManager::Get().GetTransientDescriptorPool(), 1, &pass.descriptor_set_layout };
            vkAllocateDescriptorSets(m_Context.GetDevice(), &allocInfo, &pass.descriptor_set);
            m_Context.SetDebugName((uint64_t)pass.descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, (d.name + "_Set2_Pass").c_str());
            
            std::vector<VkWriteDescriptorSet> writes;
            for (uint32_t i = 0; i < (uint32_t)bindings.size(); ++i)
            {
                writes.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, pass.descriptor_set, bindings[i].binding, 0, 1, bindings[i].descriptorType, &pInfo->images[i], nullptr, nullptr });
            }
            vkUpdateDescriptorSets(m_Context.GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
        }

        m_Passes[d.name] = pass;
        m_RaytracingPipelines[d.name] = &PipelineManager::Get().GetRaytracingPipeline(m_Passes[d.name], std::get<RaytracingPipelineDescription>(d.description));
    }

    void RenderGraph::CreateComputePass(RenderPassDescription& d)
    {
        ComputePass cp;
        cp.callback = std::get<ComputePassCallback>(d.callback);
        RenderPass pass { d.name, VK_NULL_HANDLE, VK_NULL_HANDLE, std::make_shared<RenderPass::PrivateInfo>(), cp };
        
        auto pInfo = pass.private_info;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        
        auto collectBinding = [&](const TransientResource& res)
        {
            if (res.type == TransientResourceType::Image && m_Images.count(res.name))
            {
                auto& img = m_Images.at(res.name);
                uint32_t b = (res.image.binding != 0xFFFFFFFF) ? res.image.binding : (uint32_t)bindings.size();
                VkImageLayout l = (res.image.type == TransientImageType::StorageImage) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                pInfo->images.push_back({ ResourceManager::Get().GetDefaultSampler(), img.view, l });
                bindings.push_back({ b, (res.image.type == TransientImageType::StorageImage) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, nullptr });
            }
        };

        for (auto& dep : d.dependencies) collectBinding(dep);
        for (auto& out : d.outputs) collectBinding(out);

        if (!bindings.empty())
        {
            VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data() };
            vkCreateDescriptorSetLayout(m_Context.GetDevice(), &layoutInfo, nullptr, &pass.descriptor_set_layout);
            
            VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, ResourceManager::Get().GetTransientDescriptorPool(), 1, &pass.descriptor_set_layout };
            vkAllocateDescriptorSets(m_Context.GetDevice(), &allocInfo, &pass.descriptor_set);
            m_Context.SetDebugName((uint64_t)pass.descriptor_set, VK_OBJECT_TYPE_DESCRIPTOR_SET, (d.name + "_Set2_Pass").c_str());
            
            std::vector<VkWriteDescriptorSet> writes;
            for (uint32_t i = 0; i < (uint32_t)bindings.size(); ++i)
            {
                writes.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, pass.descriptor_set, bindings[i].binding, 0, 1, bindings[i].descriptorType, &pInfo->images[i], nullptr, nullptr });
            }
            vkUpdateDescriptorSets(m_Context.GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
        }

        m_Passes[d.name] = pass;
        auto& computeDesc = std::get<ComputePipelineDescription>(d.description);
        for (auto& kernel : computeDesc.kernels)
        {
            m_ComputePipelines[kernel.name] = &PipelineManager::Get().GetComputePipeline(m_Passes[d.name], kernel);
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // [Execution Logic] Dispatching Passes to GPU
    // ---------------------------------------------------------------------------------------------------------------------

    void RenderGraph::Execute(VkCommandBuffer cmd, uint32_t rIdx, uint32_t iIdx)
    {
        for (auto& passName : m_ExecutionOrder)
        {
            auto& pass = m_Passes[passName];
            
            if (std::holds_alternative<GraphicsPass>(pass.pass))
            {
                ExecuteGraphicsPass(cmd, rIdx, iIdx, pass);
            }
            else if (std::holds_alternative<RaytracingPass>(pass.pass))
            {
                ExecuteRaytracingPass(cmd, rIdx, pass);
            }
            else if (std::holds_alternative<ComputePass>(pass.pass))
            {
                ExecuteComputePass(cmd, rIdx, pass);
            }
            else if (std::holds_alternative<BlitPass>(pass.pass))
            {
                ExecuteBlitPass(cmd, pass);
            }
        }
    }

    void RenderGraph::ExecuteGraphicsPass(VkCommandBuffer cmd, uint32_t rIdx, uint32_t iIdx, RenderPass& p)
    {
        GraphicsPass& gp = std::get<GraphicsPass>(p.pass);
        auto& pipe = *m_GraphicsPipelines[p.name];
        
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        bool hasDepth = false;
        
        // 1. Resolve Attachments & Layout Transitions
        for (auto& att : gp.attachments)
        {
            if (att.type != TransientResourceType::Image || m_Images.find(att.name) == m_Images.end())
            {
                continue;
            }

            auto& img = m_Images.at(att.name);
            auto& access = m_ImageAccess[att.name];
            
            VkImageLayout targetLayout = VulkanUtils::IsDepthFormat(img.format) ? 
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            if (access.layout != targetLayout)
            {
                VulkanUtils::TransitionImageLayout(cmd, img.handle, img.format, access.layout, targetLayout);
                access.layout = targetLayout;
            }

            VkRenderingAttachmentInfo info{ 
                VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, img.view, targetLayout, 
                VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, 
                VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, att.image.clear_value 
            };

            if (VulkanUtils::IsDepthFormat(img.format))
            {
                depthAttachment = info;
                hasDepth = true;
            }
            else
            {
                colorAttachments.push_back(info);
            }
        }

        // 2. Start Dynamic Rendering Pass
        VkRenderingInfo renderingInfo{ 
            VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, { {0, 0}, { m_Width, m_Height } }, 
            1, 0, (uint32_t)colorAttachments.size(), colorAttachments.data(), 
            hasDepth ? &depthAttachment : nullptr, nullptr 
        };

        vkCmdBeginRendering(cmd, &renderingInfo);

        // 3. Set Dynamic State
        VkViewport viewport{ 0.0f, 0.0f, (float)m_Width, (float)m_Height, 0.0f, 1.0f };
        VkRect2D scissor{ {0, 0}, { m_Width, m_Height } };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // 4. Bind Global & Scene & Pass Descriptors (3-Set Contract)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.handle);
        VkDescriptorSet sets[] = { 
            Application::Get().GetRenderState()->GetDescriptorSet(rIdx), 
            ResourceManager::Get().GetSceneDescriptorSet(), 
            (p.descriptor_set != VK_NULL_HANDLE) ? p.descriptor_set : m_Context.GetEmptyDescriptorSet() 
        };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.layout, 0, 3, sets, 0, nullptr);

        // 5. Execute Callback
        ExecuteGraphicsCallback exec = [&](std::string n, GraphicsExecutionCallback cb)
        {
            GraphicsExecutionContext ctx(cmd, pipe.layout);
            cb(ctx);
        };
        gp.callback(exec);

        vkCmdEndRendering(cmd);
    }

    void RenderGraph::ExecuteRaytracingPass(VkCommandBuffer cmd, uint32_t rIdx, RenderPass& p)
    {
        auto& desc = m_PassDescriptions.at(p.name);
        
        // 1. Resolve Transitions
        auto transition = [&](const TransientResource& res)
        {
            if (res.type != TransientResourceType::Image || m_Images.find(res.name) == m_Images.end())
            {
                return;
            }
            auto& img = m_Images.at(res.name);
            auto& access = m_ImageAccess[res.name];
            VkImageLayout target = (res.image.type == TransientImageType::StorageImage) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            
            if (access.layout != target)
            {
                VulkanUtils::TransitionImageLayout(cmd, img.handle, img.format, access.layout, target);
                access.layout = target;
            }
        };

        for (auto& res : desc.dependencies) transition(res);
        for (auto& res : desc.outputs) transition(res);

        // 2. Bind & Trace
        RaytracingPass& rp = std::get<RaytracingPass>(p.pass);
        auto& pipe = *m_RaytracingPipelines[p.name];
        
        ExecuteRaytracingCallback exec = [&](std::string n, RaytracingExecutionCallback cb)
        {
            RaytracingExecutionContext ctx(cmd, m_Context, pipe); 
            VkDescriptorSet sets[] = { 
                Application::Get().GetRenderState()->GetDescriptorSet(rIdx), 
                ResourceManager::Get().GetSceneDescriptorSet(), 
                (p.descriptor_set != VK_NULL_HANDLE) ? p.descriptor_set : m_Context.GetEmptyDescriptorSet() 
            };
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipe.layout, 0, 3, sets, 0, nullptr);
            cb(ctx); 
        };
        rp.callback(exec);
    }

    void RenderGraph::ExecuteComputePass(VkCommandBuffer cmd, uint32_t rIdx, RenderPass& p)
    {
        auto& desc = m_PassDescriptions.at(p.name);
        
        // 1. Resolve Transitions
        auto transition = [&](const TransientResource& res)
        {
            if (res.type != TransientResourceType::Image || m_Images.find(res.name) == m_Images.end())
            {
                return;
            }
            auto& img = m_Images.at(res.name);
            auto& access = m_ImageAccess[res.name];
            VkImageLayout target = (res.image.type == TransientImageType::StorageImage) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            
            if (access.layout != target)
            {
                VulkanUtils::TransitionImageLayout(cmd, img.handle, img.format, access.layout, target);
                access.layout = target;
            }
        };

        for (auto& dep : desc.dependencies) transition(dep);
        for (auto& out : desc.outputs) transition(out);

        // 2. Execute
        ComputePass& cp = std::get<ComputePass>(p.pass);
        ComputeExecutionContext ctx(cmd, p, *this, rIdx);
        cp.callback(ctx);
    }

    void RenderGraph::ExecuteBlitPass(VkCommandBuffer cmd, RenderPass& p)
    {
        auto& bp = std::get<BlitPass>(p.pass);
        auto& src = m_Images.at(bp.srcName);
        auto& srcAccess = m_ImageAccess[bp.srcName];
        
        VkImage dst = VK_NULL_HANDLE;
        VkFormat dstFormat = VK_FORMAT_UNDEFINED;
        VkImageLayout currentDstLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (bp.dstName == RS::RENDER_OUTPUT)
        {
            dst = m_Context.GetSwapChainImages()[Application::Get().GetCurrentImageIndex()];
            dstFormat = m_Context.GetSwapChainImageFormat();
            currentDstLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        else
        {
            dst = m_Images.at(bp.dstName).handle;
            dstFormat = m_Images.at(bp.dstName).format;
            currentDstLayout = m_ImageAccess[bp.dstName].layout;
            if (currentDstLayout == VK_IMAGE_LAYOUT_UNDEFINED)
            {
                currentDstLayout = VK_IMAGE_LAYOUT_GENERAL;
            }
        }

        VkImageBlit blit{};
        blit.srcOffsets[1] = { (int32_t)src.width, (int32_t)src.height, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[1] = { (int32_t)m_Width, (int32_t)m_Height, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;

        VulkanUtils::TransitionImageLayout(cmd, src.handle, src.format, srcAccess.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VulkanUtils::TransitionImageLayout(cmd, dst, dstFormat, currentDstLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        
        vkCmdBlitImage(cmd, src.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        
        VulkanUtils::TransitionImageLayout(cmd, src.handle, src.format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcAccess.layout);
        
        if (bp.dstName == RS::RENDER_OUTPUT)
        {
            VulkanUtils::TransitionImageLayout(cmd, dst, dstFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        else
        {
            VulkanUtils::TransitionImageLayout(cmd, dst, dstFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, currentDstLayout);
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // [Utilities]
    // ---------------------------------------------------------------------------------------------------------------------

    bool RenderGraph::ContainsImage(std::string n)
    {
        return m_Images.count(n);
    }

    const GraphImage& RenderGraph::GetImage(std::string n) const
    {
        return m_Images.at(n);
    }

    ImageAccess& RenderGraph::GetImageAccess(const std::string& n)
    {
        return m_ImageAccess[n];
    }

    std::vector<std::string> RenderGraph::GetColorAttachments() const
    {
        std::vector<std::string> attachments;
        for (const auto& [name, img] : m_Images)
        {
            if (name != RS::RENDER_OUTPUT)
            {
                attachments.push_back(name);
            }
        }
        std::sort(attachments.begin(), attachments.end());
        return attachments;
    }

    void RenderGraph::DrawPerformanceStatistics()
    {
        ImGui::Text("RenderGraph Stats:");
        ImGui::BulletText("Pass Count: %llu", m_Passes.size());
        ImGui::BulletText("Virtual Resource Count: %llu", m_Images.size());
    }

    void RenderGraph::RegisterExternalResource(const std::string& n, const ImageDescription& d) {}
    void RenderGraph::SetExternalResource(const std::string& n, VkImage h, VkImageView v, VkImageLayout cl, VkAccessFlags ca, VkPipelineStageFlags cs) {}
    VkFormat RenderGraph::GetImageFormat(std::string n) { return VK_FORMAT_UNDEFINED; }
}
