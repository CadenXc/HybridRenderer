#include "pch.h"
#include "rendering/graph/core/RenderGraph.h"
#include "gfx/utils/VulkanBarrier.h"
#include "gfx/utils/VulkanShaderUtils.h"
#include "gfx/utils/VulkanDescriptorUtils.h"
#include "rendering/pipelines/common/RenderPathUtils.h"
#include <imgui.h>
#include <deque>
#include <queue>
#include <unordered_set>
#include <algorithm>

namespace Chimera {

    RenderGraph::RenderGraph(std::shared_ptr<VulkanContext> m_Context, ResourceManager &m_ResourceManager) : 
        m_Context(m_Context), 
        m_ResourceManager(m_ResourceManager) 
    {}

    RenderGraph::~RenderGraph() 
    {
        DestroyResources();
    }

    void RenderGraph::DestroyResources() 
    {
        if (m_Context->GetDevice() == VK_NULL_HANDLE) return;
        
        vkDeviceWaitIdle(m_Context->GetDevice());

        for (auto &[_, render_pass] : m_Passes) 
        {
            if (render_pass.descriptor_set_layout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(m_Context->GetDevice(), render_pass.descriptor_set_layout, nullptr);
            
            if (std::holds_alternative<GraphicsPass>(render_pass.pass)) 
            {
                GraphicsPass &graphics_pass = std::get<GraphicsPass>(render_pass.pass);
                for (VkFramebuffer &framebuffer : graphics_pass.framebuffers) 
                {
                    if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(m_Context->GetDevice(), framebuffer, nullptr);
                }
                if (graphics_pass.handle != VK_NULL_HANDLE) vkDestroyRenderPass(m_Context->GetDevice(), graphics_pass.handle, nullptr);
            }
        }

        for (auto &[_, pipeline] : m_GraphicsPipelines) 
        {
            if (pipeline.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_Context->GetDevice(), pipeline.layout, nullptr);
            if (pipeline.handle != VK_NULL_HANDLE) vkDestroyPipeline(m_Context->GetDevice(), pipeline.handle, nullptr);
        }

        for (auto &[_, pipeline] : m_RaytracingPipelines) 
        {
            if (pipeline.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_Context->GetDevice(), pipeline.layout, nullptr);
            if (pipeline.handle != VK_NULL_HANDLE) vkDestroyPipeline(m_Context->GetDevice(), pipeline.handle, nullptr);
        }

        for (auto &[_, pipeline] : m_ComputePipelines) 
        {
            if (pipeline.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_Context->GetDevice(), pipeline.layout, nullptr);
            if (pipeline.handle != VK_NULL_HANDLE) vkDestroyPipeline(m_Context->GetDevice(), pipeline.handle, nullptr);
        }

        for (auto &[_, image] : m_Images) m_ResourceManager.DestroyGraphImage(image);
        m_Buffers.clear();

        if (m_TimestampQueryPool != VK_NULL_HANDLE) vkDestroyQueryPool(m_Context->GetDevice(), m_TimestampQueryPool, nullptr);

        m_DescriptorSetCache.clear();
        m_Readers.clear(); m_Writers.clear(); m_Passes.clear(); m_PassDescriptions.clear();
        m_GraphicsPipelines.clear(); m_RaytracingPipelines.clear(); m_ComputePipelines.clear();
        m_Images.clear(); m_ImageAccess.clear(); m_PassTimestamps.clear();
    }

    void RenderGraph::AddGraphicsPass(const char *render_pass_name, std::vector<TransientResource> dependencies, 
        std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines, 
        GraphicsPassCallback callback) 
    {
        m_PassDescriptions[render_pass_name] = { render_pass_name, dependencies, outputs, GraphicsPassDescription { pipelines, callback } };
    }

    void RenderGraph::AddRaytracingPass(const char *render_pass_name, std::vector<TransientResource> dependencies, 
        std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline, 
        RaytracingPassCallback callback) 
    {
        m_PassDescriptions[render_pass_name] = { render_pass_name, dependencies, outputs, RaytracingPassDescription { pipeline, callback } };
    }

    void RenderGraph::AddComputePass(const char *render_pass_name, std::vector<TransientResource> dependencies, 
        std::vector<TransientResource> outputs, ComputePipelineDescription pipeline, ComputePassCallback callback) 
    {
        m_PassDescriptions[render_pass_name] = { render_pass_name, dependencies, outputs, ComputePassDescription { pipeline, callback } };
    }

    void RenderGraph::Build() 
    {
        DestroyResources();
        m_ResourceStates.clear(); m_ResourceLifetimes.clear(); m_ImageDescriptions.clear();
        m_BufferDescriptions.clear(); m_Readers.clear(); m_Writers.clear();

        for (auto& [name, desc] : m_PassDescriptions) 
        {
            for (const auto& dep : desc.dependencies) m_Readers[dep.name].emplace_back(name);
            for (const auto& out : desc.outputs) m_Writers[out.name].emplace_back(name);
        }

        FindExecutionOrder();

        for (uint32_t i = 0; i < (uint32_t)m_ExecutionOrder.size(); ++i) 
        {
            const std::string& pass_name = m_ExecutionOrder[i];
            const auto& pass_desc = m_PassDescriptions[pass_name];

            auto process_resource = [&](const TransientResource& res) 
            {
                if (strcmp(res.name, "RENDER_OUTPUT") == 0) return;
                auto& lifetime = m_ResourceLifetimes[res.name];
                if (i < lifetime.first_pass) lifetime.first_pass = i;
                if (i > lifetime.last_pass) lifetime.last_pass = i;

                if (res.type == TransientResourceType::Image && !m_ImageDescriptions.count(res.name)) 
                {
                    uint32_t w = res.image.width ? res.image.width : m_Context->GetSwapChainExtent().width;
                    uint32_t h = res.image.height ? res.image.height : m_Context->GetSwapChainExtent().height;
                    VkImageUsageFlags usage = VulkanUtils::IsDepthFormat(res.image.format) ? 
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT :
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                    m_ImageDescriptions[res.name] = { w, h, res.image.format, usage, VK_SAMPLE_COUNT_1_BIT };
                }
                else if (res.type == TransientResourceType::Buffer && !m_BufferDescriptions.count(res.name))
                {
                    m_BufferDescriptions[res.name] = { (VkDeviceSize)res.buffer.stride * res.buffer.count, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO };
                }
            };
            for (const auto& dep : pass_desc.dependencies) process_resource(dep);
            for (const auto& out : pass_desc.outputs) process_resource(out);
        }

        std::vector<std::string> sorted_resources;
        for (const auto& [name, _] : m_ResourceLifetimes) sorted_resources.push_back(name);
        std::sort(sorted_resources.begin(), sorted_resources.end(), [&](const std::string& a, const std::string& b) {
            return m_ResourceLifetimes[a].first_pass < m_ResourceLifetimes[b].first_pass;
        });

        m_PhysicalImages.clear(); m_PhysicalBuffers.clear();
        for (const auto& name : sorted_resources) 
        {
            const auto& lifetime = m_ResourceLifetimes[name];
            if (m_ImageDescriptions.count(name)) 
            {
                const auto& desc = m_ImageDescriptions[name];
                bool reused = false;
                for (auto& physical : m_PhysicalImages) {
                    if (physical.last_used_pass < lifetime.first_pass && ImageDescription{physical.image.width, physical.image.height, physical.image.format, physical.image.usage, VK_SAMPLE_COUNT_1_BIT} == desc) {
                        m_Images[name] = physical.image; physical.last_used_pass = lifetime.last_pass; reused = true; break;
                    }
                }
                if (!reused) {
                    GraphImage new_image = m_ResourceManager.CreateGraphImage(desc.width, desc.height, desc.format, desc.usage, VK_IMAGE_LAYOUT_UNDEFINED, desc.samples);
                    m_Images[name] = new_image; m_PhysicalImages.push_back({ new_image, lifetime.last_pass });
                    m_ResourceManager.TagImage(new_image, name.c_str());
                }
                m_ImageAccess[name] = { VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
            }
            else if (m_BufferDescriptions.count(name)) 
            {
                const auto& desc = m_BufferDescriptions[name];
                bool reused = false;
                for (auto& physical : m_PhysicalBuffers) {
                    if (physical.last_used_pass < lifetime.first_pass && physical.buffer->GetSize() >= desc.size) {
                        m_Buffers[name] = physical.buffer; physical.last_used_pass = lifetime.last_pass; reused = true; break;
                    }
                }
                if (!reused) {
                    auto new_buffer = std::make_shared<Buffer>(m_Context->GetAllocator(), desc.size, desc.usage, desc.memory_usage);
                    m_Buffers[name] = new_buffer; m_PhysicalBuffers.push_back({ new_buffer, lifetime.last_pass });
                }
            }
        }

        for (const auto& pass_name : m_ExecutionOrder) 
        {
            auto& pass_desc = m_PassDescriptions[pass_name];
            if (std::holds_alternative<GraphicsPassDescription>(pass_desc.description)) CreateGraphicsPass(pass_desc);
            else if (std::holds_alternative<RaytracingPassDescription>(pass_desc.description)) CreateRaytracingPass(pass_desc);
            else if (std::holds_alternative<ComputePassDescription>(pass_desc.description)) CreateComputePass(pass_desc);
        }

        VkQueryPoolCreateInfo qpi { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_TIMESTAMP, static_cast<uint32_t>(m_ExecutionOrder.size()) * 2 };
        vkCreateQueryPool(m_Context->GetDevice(), &qpi, nullptr, &m_TimestampQueryPool);
    }

    void RenderGraph::Execute(VkCommandBuffer command_buffer, uint32_t resource_idx, uint32_t image_idx) 
    {
        if (m_TimestampQueryPool) 
        {
            uint32_t timestamp_count = static_cast<uint32_t>(m_ExecutionOrder.size()) * 2;
            vkCmdResetQueryPool(command_buffer, m_TimestampQueryPool, 0, timestamp_count);
        }

        for (int i = 0; i < (int)m_ExecutionOrder.size(); ++i) 
        {
            std::string &pass_name = m_ExecutionOrder[i];
            RenderPass &render_pass = m_Passes[pass_name];

            if (m_TimestampQueryPool) vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampQueryPool, (i * 2));
            
            InsertBarriers(command_buffer, render_pass);

            if (std::holds_alternative<GraphicsPass>(render_pass.pass)) 
            {
                ExecuteGraphicsPass(command_buffer, resource_idx, image_idx, render_pass);
            }
            else if (std::holds_alternative<ComputePass>(render_pass.pass)) 
            {
                ExecuteComputePass(command_buffer, resource_idx, render_pass);
            }
            else if (std::holds_alternative<RaytracingPass>(render_pass.pass)) 
            {
                ExecuteRaytracingPass(command_buffer, resource_idx, render_pass);
            }

            if (m_TimestampQueryPool) vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampQueryPool, (i * 2) + 1);
        }
    }

    void RenderGraph::GatherPerformanceStatistics() 
    {
        if (!m_TimestampQueryPool || m_ExecutionOrder.empty()) return;
        uint32_t count = static_cast<uint32_t>(m_ExecutionOrder.size()) * 2;
        std::vector<uint64_t> ts(count);
        if (vkGetQueryPoolResults(m_Context->GetDevice(), m_TimestampQueryPool, 0, count, sizeof(uint64_t) * count, ts.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) 
        {
            float period = m_Context->GetDeviceProperties().limits.timestampPeriod;
            for (size_t i = 0; i < m_ExecutionOrder.size(); ++i) 
            {
                m_PassTimestamps[m_ExecutionOrder[i]] = (double)(ts[i * 2 + 1] - ts[i * 2]) * (double)period / 1000000.0;
            }
        }
    }

    void RenderGraph::DrawPerformanceStatistics() 
    {
        ImGui::Begin("Render Graph Statistics");
        if (ImGui::BeginTable("PassTimestamps", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) 
        {
            ImGui::TableSetupColumn("Pass Name"); ImGui::TableSetupColumn("Time (ms)"); ImGui::TableHeadersRow();
            double total = 0.0;
            for (const auto& name : m_ExecutionOrder) 
            {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("%s", name.c_str());
                ImGui::TableSetColumnIndex(1); double t = m_PassTimestamps[name]; ImGui::Text("%.4f", t); total += t;
            }
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Separator(); ImGui::Text("Total GPU Time");
            ImGui::TableSetColumnIndex(1); ImGui::Separator(); ImGui::Text("%.4f ms", total);
            ImGui::EndTable();
        }
        ImGui::End();
    }

    void RenderGraph::CreateGraphicsPass(const RenderPassDescription &pass_description) 
    {
        const GraphicsPassDescription &gp_desc = std::get<GraphicsPassDescription>(pass_description.description);
        RenderPass rp { .name = pass_description.name, .pass = GraphicsPass { .callback = gp_desc.callback } };
        GraphicsPass &gp = std::get<GraphicsPass>(rp.pass);

        uint32_t color_count = 0, total_count = 0;
        for (const auto &out : pass_description.outputs) 
        { 
            if (out.type == TransientResourceType::Image && out.image.type == TransientImageType::AttachmentImage) 
            { 
                if (!VulkanUtils::IsDepthFormat(out.image.format)) color_count++; 
                total_count++; 
            } 
        }
        
        std::vector<VkAttachmentDescription> atts(total_count);
        std::vector<VkAttachmentReference> color_refs(color_count);
        gp.attachments.resize(total_count);

        VkAttachmentReference depth_ref{};
        VkSubpassDescription sub_desc{ .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS };
        std::vector<VkDescriptorSetLayoutBinding> bindings; std::vector<VkDescriptorImageInfo> descs;

        auto add_res = [&](const TransientResource &res) 
        {
            if (res.type == TransientResourceType::Image) 
            {
                switch (res.image.type) 
                {
                case TransientImageType::AttachmentImage: {
                    bool is_out = !strcmp(res.name, "RENDER_OUTPUT");
                    VkImageLayout layout = GetRequiredImageLayout(res);
                    gp.attachments[res.image.binding] = res;
                    atts[res.image.binding] = { .format = is_out ? m_Context->GetSwapChainImageFormat() : res.image.format, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = is_out ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : layout };
                    if (VulkanUtils::IsDepthFormat(res.image.format)) { depth_ref = { (uint32_t)res.image.binding, layout }; sub_desc.pDepthStencilAttachment = &depth_ref; }
                    else color_refs[res.image.binding] = { (uint32_t)res.image.binding, layout };
                } break;
                case TransientImageType::SampledImage:
                    descs.push_back(VulkanUtils::DescriptorImageInfo(m_Images[res.name].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_ResourceManager.GetDefaultSampler()));
                    bindings.push_back(VulkanUtils::DescriptorSetLayoutBinding(res.image.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT));
                    break;
                default: break;
                }
            }
        };
        for (const auto &dep : pass_description.dependencies) add_res(dep);
        for (const auto &out : pass_description.outputs) add_res(out);

        if (!bindings.empty()) 
        {
            DescriptorSetKey key{ descs };
            if (m_DescriptorSetCache.count(key)) rp.descriptor_set = m_DescriptorSetCache[key];
            else {
                VkDescriptorSetLayoutCreateInfo li { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data() };
                vkCreateDescriptorSetLayout(m_Context->GetDevice(), &li, nullptr, &rp.descriptor_set_layout);
                VkDescriptorSetAllocateInfo ai { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_ResourceManager.GetTransientDescriptorPool(), 1, &rp.descriptor_set_layout };
                vkAllocateDescriptorSets(m_Context->GetDevice(), &ai, &rp.descriptor_set);
                std::vector<VkWriteDescriptorSet> writes;
                for (uint32_t i=0; i<descs.size(); ++i) writes.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, rp.descriptor_set, bindings[i].binding, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &descs[i] });
                vkUpdateDescriptorSets(m_Context->GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
                m_DescriptorSetCache[key] = rp.descriptor_set;
            }
        }
        sub_desc.colorAttachmentCount = (uint32_t)color_refs.size(); sub_desc.pColorAttachments = color_refs.data();
        VkSubpassDependency s_dep { VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
        VkRenderPassCreateInfo rpi { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, (uint32_t)atts.size(), atts.data(), 1, &sub_desc, 1, &s_dep };
        vkCreateRenderPass(m_Context->GetDevice(), &rpi, nullptr, &gp.handle);
        for (auto &p : gp_desc.pipeline_descriptions) m_GraphicsPipelines[p.name] = VulkanUtils::CreateGraphicsPipeline(m_Context, m_ResourceManager, rp, p);
        m_Passes[rp.name] = rp;
    }

    void RenderGraph::InsertBarriers(VkCommandBuffer cb, RenderPass &rp) 
    {
        RenderPassDescription &desc = m_PassDescriptions[rp.name];
        for (const auto &dep : desc.dependencies) 
        {
            if (dep.type != TransientResourceType::Image || !m_Images.count(dep.name)) continue;
            ResourceState &s = m_ResourceStates[dep.name];
            VkImageLayout rl = GetRequiredImageLayout(dep); VkAccessFlags ra = GetRequiredAccessFlags(dep); VkPipelineStageFlags rs = GetRequiredPipelineStageFlags(dep);
            if (s.layout != rl || s.access_flags != ra) 
            {
                VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, s.access_flags, ra, s.layout, rl, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, m_Images[dep.name].handle, { (VkImageAspectFlags)(VulkanUtils::IsDepthFormat(dep.image.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS } };
                vkCmdPipelineBarrier(cb, s.stage_flags, rs, 0, 0, nullptr, 0, nullptr, 1, &b);
                s = { rl, ra, rs, s.is_written_in_frame };
            }
        }
        for (const auto &out : desc.outputs) 
        {
            if (out.type != TransientResourceType::Image) continue;
            m_ResourceStates[out.name] = { GetRequiredImageLayout(out), GetRequiredAccessFlags(out), GetRequiredPipelineStageFlags(out), true };
        }
    }

    void RenderGraph::ExecuteGraphicsPass(VkCommandBuffer cb, uint32_t r_idx, uint32_t i_idx, RenderPass &rp) 
    {
        GraphicsPass &gp = std::get<GraphicsPass>(rp.pass);
        VkFramebuffer &fb = gp.framebuffers[r_idx];
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(m_Context->GetDevice(), fb, nullptr);
        std::vector<VkImageView> views; std::vector<VkClearValue> clears; uint32_t w = 0, h = 0;
        for (auto &at : gp.attachments) 
        {
            if (!strcmp(at.name, "RENDER_OUTPUT")) { views.push_back(m_Context->GetSwapChainImageViews()[i_idx]); w = m_Context->GetSwapChainExtent().width; h = m_Context->GetSwapChainExtent().height; }
            else { views.push_back(m_Images[at.name].view); w = m_Images[at.name].width; h = m_Images[at.name].height; }
            clears.push_back(at.image.clear_value);
        }
        VkFramebufferCreateInfo fb_i = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, gp.handle, (uint32_t)views.size(), views.data(), w, h, 1 };
        vkCreateFramebuffer(m_Context->GetDevice(), &fb_i, nullptr, &fb);
        VkRenderPassBeginInfo rb = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, gp.handle, fb, { {0, 0}, {w, h} }, (uint32_t)clears.size(), clears.data() };
        vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
        gp.callback([&](std::string p_name, GraphicsExecutionCallback ex) {
            GraphicsPipeline &p = m_GraphicsPipelines[p_name];
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.handle);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &m_ResourceManager.GetGlobalDescriptorSet0(), 0, nullptr);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 1, 1, &m_ResourceManager.GetGlobalDescriptorSet1(), 0, nullptr);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 2, 1, &m_ResourceManager.GetGlobalDescriptorSet(r_idx), 0, nullptr);
            if (rp.descriptor_set != VK_NULL_HANDLE) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 3, 1, &rp.descriptor_set, 0, nullptr);
            GraphicsExecutionContext ctx(cb, m_ResourceManager, p); ex(ctx);
        });
        vkCmdEndRenderPass(cb);
    }

    void RenderGraph::ExecuteComputePass(VkCommandBuffer cb, uint32_t r_idx, RenderPass &rp) 
    {
        ComputePass &cp = std::get<ComputePass>(rp.pass);
        ComputePipeline &p = m_ComputePipelines[rp.name];
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p.handle);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &m_ResourceManager.GetGlobalDescriptorSet0(), 0, nullptr);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 1, 1, &m_ResourceManager.GetGlobalDescriptorSet1(), 0, nullptr);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 2, 1, &m_ResourceManager.GetGlobalDescriptorSet(r_idx), 0, nullptr);
        if (rp.descriptor_set != VK_NULL_HANDLE) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 3, 1, &rp.descriptor_set, 0, nullptr);
        ComputeExecutionContext ctx(cb, m_ResourceManager, p); cp.callback(ctx);
    }

    void RenderGraph::ExecuteRaytracingPass(VkCommandBuffer cb, uint32_t r_idx, RenderPass &rp) 
    {
        RaytracingPass &rt = std::get<RaytracingPass>(rp.pass);
        rt.callback([&](std::string name, RaytracingExecutionCallback ex) {
            RaytracingPipeline &p = m_RaytracingPipelines[name];
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, p.handle);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, p.layout, 0, 1, &m_ResourceManager.GetGlobalDescriptorSet0(), 0, nullptr);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, p.layout, 1, 1, &m_ResourceManager.GetGlobalDescriptorSet1(), 0, nullptr);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, p.layout, 2, 1, &m_ResourceManager.GetGlobalDescriptorSet(r_idx), 0, nullptr);
            if (rp.descriptor_set != VK_NULL_HANDLE) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, p.layout, 3, 1, &rp.descriptor_set, 0, nullptr);
            RaytracingExecutionContext ctx(cb, m_ResourceManager, p); ex(ctx);
        });
    }

    void RenderGraph::CreateRaytracingPass(const RenderPassDescription &desc) 
    {
        const RaytracingPassDescription &rt_desc = std::get<RaytracingPassDescription>(desc.description);
        RenderPass rp { .name = desc.name, .pass = RaytracingPass { .callback = rt_desc.callback } };
        m_RaytracingPipelines[desc.name] = VulkanUtils::CreateRaytracingPipeline(m_Context, m_ResourceManager, rp, rt_desc.pipeline_description, m_Context->GetRayTracingProperties());
        m_Passes[rp.name] = rp;
    }

    void RenderGraph::CreateComputePass(const RenderPassDescription &desc) 
    {
        const ComputePassDescription &cp_desc = std::get<ComputePassDescription>(desc.description);
        RenderPass rp { .name = desc.name, .pass = ComputePass { .callback = cp_desc.callback } };
        if (!cp_desc.pipeline_description.kernels.empty())
            m_ComputePipelines[desc.name] = VulkanUtils::CreateComputePipeline(m_Context, m_ResourceManager, rp, cp_desc.pipeline_description.push_constant_description, cp_desc.pipeline_description.kernels[0]);
        m_Passes[rp.name] = rp;
    }

    VkImageLayout RenderGraph::GetRequiredImageLayout(const TransientResource &res) const 
    {
        if (res.type != TransientResourceType::Image) return VK_IMAGE_LAYOUT_UNDEFINED;
        if (res.image.type == TransientImageType::AttachmentImage) return VulkanUtils::IsDepthFormat(res.image.format) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        if (res.image.type == TransientImageType::SampledImage) return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return VK_IMAGE_LAYOUT_GENERAL;
    }

    VkAccessFlags RenderGraph::GetRequiredAccessFlags(const TransientResource &res) const 
    {
        if (res.type != TransientResourceType::Image) return 0;
        if (res.image.type == TransientImageType::AttachmentImage) return VulkanUtils::IsDepthFormat(res.image.format) ? (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) : (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        if (res.image.type == TransientImageType::SampledImage) return VK_ACCESS_SHADER_READ_BIT;
        return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }

    VkPipelineStageFlags RenderGraph::GetRequiredPipelineStageFlags(const TransientResource &res) const 
    {
        if (res.type != TransientResourceType::Image) return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (res.image.type == TransientImageType::AttachmentImage) return VulkanUtils::IsDepthFormat(res.image.format) ? (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT) : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    void RenderGraph::CopyImage(VkCommandBuffer cb, std::string src, GraphImage dst) 
    {
        RenderPathUtils::BlitToSwapchain(cb, m_Context, m_Images[src].handle, dst.handle, {dst.width, dst.height});
    }
    bool RenderGraph::ContainsImage(std::string name) { return m_Images.count(name) > 0; }
    VkFormat RenderGraph::GetImageFormat(std::string name) { return m_Images[name].format; }
    std::vector<std::string> RenderGraph::GetColorAttachments() { std::vector<std::string> res; for(auto& [n, i] : m_Images) if(!VulkanUtils::IsDepthFormat(i.format)) res.push_back(n); return res; }
}


