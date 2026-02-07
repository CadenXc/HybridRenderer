#include "pch.h"
#include "RenderGraph.h"
#include "GraphicsExecutionContext.h"
#include "RaytracingExecutionContext.h"
#include "ComputeExecutionContext.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Backend/ShaderMetadata.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/RenderState.h"
#include "Renderer/Backend/VulkanCommon.h"
#include "Utils/VulkanBarrier.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Core/Application.h"
#include <deque>
#include <imgui.h>

namespace Chimera {

    static void ResolveBindings(std::vector<TransientResource>& resources, const std::string& layoutName) {
        if (layoutName.empty()) return;
        const auto& layout = ShaderLibrary::GetLayout(layoutName);
        for (auto& res : resources) {
            if (layout.HasResource(res.name)) {
                const auto& meta = layout.GetResource(res.name);
                switch (res.type) {
                    case TransientResourceType::Image: res.image.binding = meta.binding; break;
                    case TransientResourceType::Buffer: res.buffer.binding = meta.binding; break;
                    case TransientResourceType::Sampler: 
                        res.buffer.binding = meta.binding; 
                        res.buffer.count = meta.count; 
                        break;
                    case TransientResourceType::Storage: res.buffer.binding = meta.binding; break;
                    case TransientResourceType::AccelerationStructure: res.as.binding = meta.binding; break;
                }
            }
        }
    }

    void RenderGraph::AddGraphicsPass(const GraphicsPassSpecification& spec) {
        AddGraphicsPass(spec.Name, spec.Dependencies, spec.Outputs, spec.Pipelines, spec.Callback, spec.ShaderLayout);
    }

    void RenderGraph::AddRaytracingPass(const RaytracingPassSpecification& spec) {
        AddRaytracingPass(spec.Name, spec.Dependencies, spec.Outputs, spec.Pipeline, spec.Callback, spec.ShaderLayout);
    }

    void RenderGraph::AddComputePass(const ComputePassSpecification& spec) {
        AddComputePass(spec.Name, spec.Dependencies, spec.Outputs, spec.Pipeline, spec.Callback, spec.ShaderLayout);
    }

    RenderGraph::RenderGraph(VulkanContext& context, ResourceManager& resourceManager, PipelineManager& pipelineManager, uint32_t width, uint32_t height)
        : m_Context(context), m_ResourceManager(resourceManager), m_PipelineManager(pipelineManager), m_Width(width), m_Height(height)
    {
        VkQueryPoolCreateInfo queryPoolInfo{};
        queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = 128; 
        if (vkCreateQueryPool(m_Context.GetDevice(), &queryPoolInfo, nullptr, &m_TimestampQueryPool) != VK_SUCCESS) {
            CH_CORE_ERROR("RenderGraph: Failed to create timestamp query pool!");
        }
    }

    RenderGraph::~RenderGraph() {
        DestroyResources(true); 
        if (m_TimestampQueryPool != VK_NULL_HANDLE) vkDestroyQueryPool(m_Context.GetDevice(), m_TimestampQueryPool, nullptr);
    }

    void RenderGraph::DestroyResources(bool forceAll) {
        vkDeviceWaitIdle(m_Context.GetDevice());
        
        auto it = m_Images.begin();
        while (it != m_Images.end()) {
            if (forceAll || !it->second.is_external) {
                if (!it->second.is_external && it->second.handle != VK_NULL_HANDLE) {
                    m_ResourceManager.DestroyGraphImage(it->second);
                }
                it = m_Images.erase(it);
            } else {
                ++it;
            }
        }

        m_ImageAccess.clear(); 
        for (auto& pair : m_Passes) {
            RenderPass& renderPass = pair.second;
            if (renderPass.descriptor_set_layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(m_Context.GetDevice(), renderPass.descriptor_set_layout, nullptr);
            }

            if (std::holds_alternative<GraphicsPass>(renderPass.pass)) {
                auto& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
                vkDestroyRenderPass(m_Context.GetDevice(), graphicsPass.handle, nullptr);
                for (auto framebuffer : graphicsPass.framebuffers) vkDestroyFramebuffer(m_Context.GetDevice(), framebuffer, nullptr);
            }
        }
        m_Passes.clear();
        m_GraphicsPipelines.clear();
        m_RaytracingPipelines.clear();
        m_ComputePipelines.clear();
        m_SamplerArrays.clear();
        if (m_SharedMemory != VK_NULL_HANDLE) { 
            vkFreeMemory(m_Context.GetDevice(), m_SharedMemory, nullptr); 
            m_SharedMemory = VK_NULL_HANDLE; 
        }
    }

    void RenderGraph::AddGraphicsPass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
        std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines,
        GraphicsPassCallback callback, const std::string& shaderLayoutName) 
    {
        RenderPassDescription desc;
        desc.name = renderPassName;
        desc.dependencies = dependencies;
        desc.outputs = outputs;
        desc.description = GraphicsPassDescription{ pipelines, callback };
        
        ResolveBindings(desc.dependencies, shaderLayoutName);
        ResolveBindings(desc.outputs, shaderLayoutName);

        m_PassDescriptions[renderPassName] = desc;
        for (auto& res : dependencies) m_Readers[res.name].push_back(renderPassName);
        for (auto& res : outputs) m_Writers[res.name].push_back(renderPassName);
    }

    void RenderGraph::AddRaytracingPass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
        std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline,
        RaytracingPassCallback callback, const std::string& shaderLayoutName) 
    {
        RenderPassDescription desc;
        desc.name = renderPassName;
        desc.dependencies = dependencies;
        desc.outputs = outputs;
        desc.description = RaytracingPassDescription{ pipeline, callback };

        ResolveBindings(desc.dependencies, shaderLayoutName);
        ResolveBindings(desc.outputs, shaderLayoutName);

        m_PassDescriptions[renderPassName] = desc;
        for (auto& res : dependencies) m_Readers[res.name].push_back(renderPassName);
        for (auto& res : outputs) m_Writers[res.name].push_back(renderPassName);
    }

    void RenderGraph::AddComputePass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
        std::vector<TransientResource> outputs, ComputePipelineDescription pipeline,
        ComputePassCallback callback, const std::string& shaderLayoutName) 
    {
        RenderPassDescription desc;
        desc.name = renderPassName;
        desc.dependencies = dependencies;
        desc.outputs = outputs;
        desc.description = ComputePassDescription{ pipeline, callback };

        ResolveBindings(desc.dependencies, shaderLayoutName);
        ResolveBindings(desc.outputs, shaderLayoutName);

        m_PassDescriptions[renderPassName] = desc;
        for (auto& res : dependencies) m_Readers[res.name].push_back(renderPassName);
        for (auto& res : outputs) m_Writers[res.name].push_back(renderPassName);
    }

    void RenderGraph::AddBlitPass(const std::string& renderPassName, const std::string& srcImageName, const std::string& dstImageName, VkFormat srcFormat, VkFormat dstFormat) {
        RenderPassDescription desc;
        desc.name = renderPassName;
        desc.dependencies = { TransientResource::Image(srcImageName, srcFormat != VK_FORMAT_UNDEFINED ? srcFormat : GetImageFormat(srcImageName)) };
        desc.outputs = { TransientResource::Image(dstImageName, dstFormat != VK_FORMAT_UNDEFINED ? dstFormat : GetImageFormat(dstImageName)) };
        
        desc.description = BlitPassDescription{};

        m_PassDescriptions[renderPassName] = desc;
        for (auto& res : desc.dependencies) m_Readers[res.name].push_back(renderPassName);
        for (auto& res : desc.outputs) m_Writers[res.name].push_back(renderPassName);
    }

    void RenderGraph::Build() {
        if (m_Width == 0 || m_Height == 0) {
            auto extent = m_Context.GetSwapChainExtent();
            m_Width = extent.width;
            m_Height = extent.height;
        }
        // CH_CORE_INFO("RenderGraph: Building with final dimensions {}x{}", m_Width, m_Height);

        // Clear layout history on every build to handle path switching correctly
        m_ImageAccess.clear();

        FindExecutionOrder();
        for (auto& name : m_ExecutionOrder) {
            auto& desc = m_PassDescriptions[name];
            for (auto& res : desc.dependencies) {
                if (res.type == TransientResourceType::Image && !ContainsImage(res.name)) {
                    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                    if (res.image.type == TransientImageType::StorageImage) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
                    m_Images[res.name] = m_ResourceManager.CreateGraphImage(m_Width, m_Height, res.image.format, usage, VK_IMAGE_LAYOUT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);
                }
            }
            for (auto& res : desc.outputs) {
                if (res.type == TransientResourceType::Image && !ContainsImage(res.name)) {
                    if (m_Images.count(res.name) && m_Images[res.name].is_external) continue;

                    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                    if (res.image.type == TransientImageType::StorageImage) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
                    else if (res.image.type == TransientImageType::AttachmentImage) {
                        if (VulkanUtils::IsDepthFormat(res.image.format)) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                        else usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                    }
                    m_Images[res.name] = m_ResourceManager.CreateGraphImage(m_Width, m_Height, res.image.format, usage, VK_IMAGE_LAYOUT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);
                }
            }
            if (std::holds_alternative<GraphicsPassDescription>(desc.description)) CreateGraphicsPass(desc);
            else if (std::holds_alternative<RaytracingPassDescription>(desc.description)) CreateRaytracingPass(desc);
            else if (std::holds_alternative<ComputePassDescription>(desc.description)) CreateComputePass(desc);
            else if (std::holds_alternative<BlitPassDescription>(desc.description)) {
                BlitPass blit;
                blit.srcName = desc.dependencies[0].name;
                blit.dstName = desc.outputs[0].name;
                m_Passes[name] = { name, VK_NULL_HANDLE, VK_NULL_HANDLE, blit };
            }
        }
    }

    void RenderGraph::Execute(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx, std::function<void(VkCommandBuffer)> uiDrawCallback) {
        if (m_ExecutionOrder.empty()) return;
        
        vkCmdResetQueryPool(commandBuffer, m_TimestampQueryPool, 0, (uint32_t)m_ExecutionOrder.size() * 2);
        m_QueryPoolReset = true;

        uint32_t queryIdx = 0;
        for (const auto& passName : m_ExecutionOrder) {
            auto& renderPass = m_Passes[passName];
            InsertBarriers(commandBuffer, renderPass, imageIdx);
            
            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampQueryPool, queryIdx++);

            if (std::holds_alternative<GraphicsPass>(renderPass.pass)) ExecuteGraphicsPass(commandBuffer, resourceIdx, imageIdx, renderPass);
            else if (std::holds_alternative<RaytracingPass>(renderPass.pass)) ExecuteRaytracingPass(commandBuffer, resourceIdx, renderPass);
            else if (std::holds_alternative<ComputePass>(renderPass.pass)) ExecuteComputePass(commandBuffer, resourceIdx, renderPass);
            else if (std::holds_alternative<BlitPass>(renderPass.pass)) {
                auto& blit = std::get<BlitPass>(renderPass.pass);
                BlitImage(commandBuffer, blit.srcName, blit.dstName, imageIdx);
            }

            vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampQueryPool, queryIdx++);
        }

        if (uiDrawCallback) {
            VkImage swapchainImage = m_Context.GetSwapChainImages()[imageIdx];
            ImageAccess current = m_ImageAccess["RENDER_OUTPUT"];
            
            VulkanUtils::InsertImageBarrier(commandBuffer, swapchainImage, VK_IMAGE_ASPECT_COLOR_BIT,
                current.layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                current.stage_flags, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                current.access_flags, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

            uiDrawCallback(commandBuffer);
        }
    }

    void RenderGraph::GatherPerformanceStatistics() {
        if (!m_QueryPoolReset) return;
        uint32_t count = (uint32_t)m_ExecutionOrder.size() * 2;
        if (count == 0) return;

        std::vector<uint64_t> timestamps(count);
        // Non-blocking query retrieval
        VkResult result = vkGetQueryPoolResults(m_Context.GetDevice(), m_TimestampQueryPool, 0, count, 
            sizeof(uint64_t) * count, timestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

        if (result == VK_SUCCESS) {
            float timestampPeriod = m_Context.GetDeviceProperties().limits.timestampPeriod;
            for (uint32_t i = 0; i < m_ExecutionOrder.size(); ++i) {
                uint64_t start = timestamps[i * 2];
                uint64_t end = timestamps[i * 2 + 1];
                if (end >= start) {
                    double duration = (end - start) * (double)timestampPeriod / 1000000.0; 
                    m_PassTimestamps[m_ExecutionOrder[i]] = duration;
                }
            }
        }
    }

    void RenderGraph::DrawPerformanceStatistics() {
        if (m_ExecutionOrder.empty()) {
            ImGui::Text("Render Graph is empty.");
            return;
        }

        double totalTime = 0.0;
        for (const auto& passName : m_ExecutionOrder) totalTime += m_PassTimestamps[passName];

        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Total GPU Time: %.4f ms", totalTime);
        ImGui::Separator();

        if (ImGui::BeginTable("PassTimestamps", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Pass Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("GPU Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto& passName : m_ExecutionOrder) {
                double time = m_PassTimestamps[passName];
                float fraction = (totalTime > 0.0) ? (float)(time / totalTime) : 0.0f;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", passName.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f ms", time);

                ImGui::TableSetColumnIndex(2);
                char label[32];
                sprintf(label, "%.1f%%", fraction * 100.0f);
                
                // Color mapping: Red for heavy passes, Green for light ones
                ImVec4 color = ImVec4(fraction * 2.0f, 1.0f - fraction, 0.2f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
                ImGui::ProgressBar(fraction, ImVec2(-1, 0), label);
                ImGui::PopStyleColor();
            }

            ImGui::EndTable();
        }
    }

    void RenderGraph::FindExecutionOrder() {
        m_ExecutionOrder.clear();
        std::unordered_map<std::string, uint32_t> inDegree;
        for (auto& pair : m_PassDescriptions) {
            const std::string& name = pair.first;
            RenderPassDescription& desc = pair.second;
            inDegree[name] = 0;
            for (auto& dep : desc.dependencies) {
                if (m_Writers.count(dep.name)) {
                    for (auto& writer : m_Writers[dep.name]) {
                        if (writer != name) inDegree[name]++;
                    }
                }
            }
        }
        std::deque<std::string> queue;
        for (auto& pair : inDegree) if (pair.second == 0) queue.push_back(pair.first);
        while (!queue.empty()) {
            std::string u = queue.front(); queue.pop_front();
            m_ExecutionOrder.push_back(u);
            for (auto& out : m_PassDescriptions[u].outputs) {
                if (m_Readers.count(out.name)) {
                    for (auto& v : m_Readers[out.name]) {
                        if (--inDegree[v] == 0) queue.push_back(v);
                    }
                }
            }
        }
    }

    static ImageAccess GetUsageState(const RenderPass& renderPass, const TransientResource& res, bool isOutput) {
        bool isGraphics = std::holds_alternative<GraphicsPass>(renderPass.pass);
        bool isRaytracing = std::holds_alternative<RaytracingPass>(renderPass.pass);
        bool isCompute = std::holds_alternative<ComputePass>(renderPass.pass);
        bool isBlit = std::holds_alternative<BlitPass>(renderPass.pass);

        if (isBlit) {
            return isOutput ? 
                ImageAccess{ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT } :
                ImageAccess{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT };
        }

        if (isOutput) {
            if (res.name == "RENDER_OUTPUT") return { VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, (VkPipelineStageFlags)VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            if (isGraphics) {
                if (VulkanUtils::IsDepthFormat(res.image.format)) return { VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, (VkPipelineStageFlags)VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT };
                return { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, (VkPipelineStageFlags)VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            }
            return { VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, (VkPipelineStageFlags)(isCompute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR) };
        }

        // Default Input states
        if (res.image.type == TransientImageType::StorageImage) return { VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, (VkPipelineStageFlags)(isCompute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : (isRaytracing ? VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)) };
        return { VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, (VkPipelineStageFlags)(isGraphics ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : (isCompute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR)) };
    }

    void RenderGraph::InsertBarriers(VkCommandBuffer commandBuffer, RenderPass& renderPass, uint32_t imageIdx) {
        auto& passDescription = m_PassDescriptions[renderPass.name];
        auto process = [&](const TransientResource& resource, bool isOutput) {
            if (resource.type == TransientResourceType::Image) {
                VkImage handle = (resource.name == "RENDER_OUTPUT") ? m_Context.GetSwapChainImages()[imageIdx] : m_Images[resource.name].handle;
                
                ImageAccess current = { VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
                if (m_ImageAccess.count(resource.name)) current = m_ImageAccess[resource.name];
                
                if (resource.name == "RENDER_OUTPUT" && !isOutput) {
                    current.layout = VK_IMAGE_LAYOUT_UNDEFINED;
                    current.access_flags = 0;
                    current.stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                }

                ImageAccess next = GetUsageState(renderPass, resource, isOutput);

                VulkanUtils::InsertImageBarrier(commandBuffer, handle, VulkanUtils::IsDepthFormat(resource.image.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, current.layout, next.layout, current.stage_flags, next.stage_flags, current.access_flags, next.access_flags);
                if (resource.name != "RENDER_OUTPUT") m_ImageAccess[resource.name] = next;
            }
        };
        for (const auto& resource : passDescription.dependencies) process(resource, false);
        for (const auto& resource : passDescription.outputs) process(resource, true);
    }

    void RenderGraph::CreatePassDescriptorSet(RenderPass& renderPass, RenderPassDescription& passDescription, VkShaderStageFlags stageFlags) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        std::vector<TransientResource> all = passDescription.dependencies;
        for (auto& out : passDescription.outputs) if (out.type != TransientResourceType::Image || out.image.type != TransientImageType::AttachmentImage) all.push_back(out);
        std::deque<VkDescriptorImageInfo> imgInfos; std::deque<VkDescriptorBufferInfo> bufInfos; std::deque<VkWriteDescriptorSetAccelerationStructureKHR> asInfos; 
        for (auto& res : all) {
            VkDescriptorSetLayoutBinding b{}; b.binding = (res.type == TransientResourceType::Image) ? res.image.binding : res.buffer.binding;
            if (res.type == TransientResourceType::AccelerationStructure) b.binding = res.as.binding;
            b.descriptorCount = (res.type == TransientResourceType::Sampler) ? res.buffer.count : 1;
            b.stageFlags = stageFlags;
            switch (res.type) {
                case TransientResourceType::Image: b.descriptorType = (res.image.type == TransientImageType::StorageImage) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
                case TransientResourceType::Buffer: case TransientResourceType::Storage: b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; break;
                case TransientResourceType::Sampler: b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
                case TransientResourceType::AccelerationStructure: b.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; break;
            }
            bindings.push_back(b);
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO }; layoutInfo.bindingCount = (uint32_t)bindings.size(); layoutInfo.pBindings = bindings.data(); vkCreateDescriptorSetLayout(m_Context.GetDevice(), &layoutInfo, nullptr, &renderPass.descriptor_set_layout);
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO }; allocInfo.descriptorPool = m_ResourceManager.GetTransientDescriptorPool(); allocInfo.descriptorSetCount = 1; allocInfo.pSetLayouts = &renderPass.descriptor_set_layout; 
        vkAllocateDescriptorSets(m_Context.GetDevice(), &allocInfo, &renderPass.descriptor_set);
        std::vector<VkWriteDescriptorSet> writes;
        for (auto& res : all) {
            VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }; w.dstSet = renderPass.descriptor_set; w.dstBinding = (res.type == TransientResourceType::Image) ? res.image.binding : res.buffer.binding; if (res.type == TransientResourceType::AccelerationStructure) w.dstBinding = res.as.binding; w.descriptorCount = (res.type == TransientResourceType::Sampler) ? res.buffer.count : 1;
            if (res.type == TransientResourceType::Image) {
                VkDescriptorImageInfo info{}; info.imageView = m_Images[res.name].view; info.imageLayout = (res.image.type == TransientImageType::StorageImage) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; info.sampler = m_ResourceManager.GetDefaultSampler();
                imgInfos.push_back(info); w.descriptorType = (res.image.type == TransientImageType::StorageImage) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &imgInfos.back();
            } else if (res.type == TransientResourceType::Sampler) {
                const auto& textures = m_ResourceManager.GetTextures(); std::vector<VkDescriptorImageInfo> infos(res.buffer.count);
                auto defaultTex = m_ResourceManager.GetDefaultTexture();
                for(uint32_t i=0; i<res.buffer.count; ++i) { 
                    infos[i].sampler = m_ResourceManager.GetDefaultSampler(); 
                    auto* tex = (i < textures.size() && textures[i]) ? textures[i].get() : defaultTex;
                    infos[i].imageView = tex->GetImageView(); 
                    infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; 
                }
                m_SamplerArrays.push_back(infos); w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = m_SamplerArrays.back().data();
            } else if (res.type == TransientResourceType::Buffer || res.type == TransientResourceType::Storage) {
                VkDescriptorBufferInfo info{}; info.buffer = res.buffer.handle; info.offset = 0; info.range = VK_WHOLE_SIZE;
                bufInfos.push_back(info); w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo = &bufInfos.back();
            } else if (res.type == TransientResourceType::AccelerationStructure) {
                VkWriteDescriptorSetAccelerationStructureKHR as{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR }; as.accelerationStructureCount = 1; as.pAccelerationStructures = &res.as.handle;
                asInfos.push_back(as); w.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; w.pNext = &asInfos.back();
            }
            writes.push_back(w);
        }
        vkUpdateDescriptorSets(m_Context.GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }

    void RenderGraph::CreateGraphicsPass(RenderPassDescription& passDescription) {
        GraphicsPassDescription& graphicsPassDescription = std::get<GraphicsPassDescription>(passDescription.description);
        GraphicsPass graphicsPass; graphicsPass.callback = graphicsPassDescription.callback;
        std::vector<VkAttachmentDescription> attachments; std::vector<VkAttachmentReference> colorRefs; VkAttachmentReference depthRef{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED }; bool isMultisampled = false;
        ParseGraphicsAttachments(passDescription, graphicsPass, attachments, colorRefs, depthRef, isMultisampled);
        VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO }; renderPassInfo.attachmentCount = (uint32_t)attachments.size(); renderPassInfo.pAttachments = attachments.data();
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = (uint32_t)colorRefs.size(); subpass.pColorAttachments = colorRefs.data(); subpass.pDepthStencilAttachment = (depthRef.attachment != VK_ATTACHMENT_UNUSED) ? &depthRef : nullptr;
        VkSubpassDependency dependency{}; dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0; dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; dependency.srcAccessMask = 0; dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        renderPassInfo.subpassCount = 1; renderPassInfo.pSubpasses = &subpass; renderPassInfo.dependencyCount = 1; renderPassInfo.pDependencies = &dependency;
        vkCreateRenderPass(m_Context.GetDevice(), &renderPassInfo, nullptr, &graphicsPass.handle);
        m_Passes[passDescription.name] = { passDescription.name, VK_NULL_HANDLE, VK_NULL_HANDLE, graphicsPass };
        RenderPass& passRef = m_Passes[passDescription.name];
        CreatePassDescriptorSet(passRef, passDescription, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        m_GraphicsPipelines[passDescription.name] = &m_PipelineManager.GetGraphicsPipeline(passRef, graphicsPassDescription.pipeline_descriptions[0]);
        CreateFramebuffers(passRef);
    }

    void RenderGraph::ParseGraphicsAttachments(RenderPassDescription& passDescription, GraphicsPass& graphicsPass,
        std::vector<VkAttachmentDescription>& attachments, std::vector<VkAttachmentReference>& colorRefs,
        VkAttachmentReference& depthRef, bool& isMultisampled)
    {
        for (auto& res : passDescription.outputs) {
            if (res.type == TransientResourceType::Image && res.image.type == TransientImageType::AttachmentImage) {
                VkAttachmentDescription att{};
                att.format = res.image.format;
                att.samples = VK_SAMPLE_COUNT_1_BIT;
                att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                
                if (VulkanUtils::IsDepthFormat(res.image.format)) {
                    att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    depthRef.attachment = (uint32_t)attachments.size();
                    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                } else {
                    att.finalLayout = (res.name == "RENDER_OUTPUT") ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    VkAttachmentReference ref{};
                    ref.attachment = (uint32_t)attachments.size();
                    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    colorRefs.push_back(ref);
                }
                attachments.push_back(att);
                graphicsPass.attachments.push_back(res);
            }
        }
    }

    void RenderGraph::CreateRaytracingPass(RenderPassDescription& passDescription) {
        RaytracingPassDescription& rtPassDescription = std::get<RaytracingPassDescription>(passDescription.description);
        RaytracingPass rtPass; rtPass.callback = rtPassDescription.callback;
        m_Passes[passDescription.name] = { passDescription.name, VK_NULL_HANDLE, VK_NULL_HANDLE, rtPass };
        RenderPass& passRef = m_Passes[passDescription.name];
        CreatePassDescriptorSet(passRef, passDescription, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR);
        m_RaytracingPipelines[passDescription.name] = &m_PipelineManager.GetRaytracingPipeline(passRef, rtPassDescription.pipeline_description);
    }

    void RenderGraph::CreateComputePass(RenderPassDescription& passDescription) {
        ComputePassDescription& computePassDescription = std::get<ComputePassDescription>(passDescription.description);
        ComputePass computePass; computePass.callback = computePassDescription.callback;
        m_Passes[passDescription.name] = { passDescription.name, VK_NULL_HANDLE, VK_NULL_HANDLE, computePass };
        RenderPass& passRef = m_Passes[passDescription.name];
        CreatePassDescriptorSet(passRef, passDescription, VK_SHADER_STAGE_COMPUTE_BIT);
        m_ComputePipelines[passDescription.name] = &m_PipelineManager.GetComputePipeline(passRef, computePassDescription.pipeline_description.push_constant_description, computePassDescription.pipeline_description.kernels[0]);
    }

    void RenderGraph::CreateFramebuffers(RenderPass& renderPass) {
        GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
        graphicsPass.framebuffers.resize(MAX_FRAMES_IN_FLIGHT);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            std::vector<VkImageView> attachments;
            for (auto& res : graphicsPass.attachments) {
                if (res.name == "RENDER_OUTPUT") attachments.push_back(m_Context.GetSwapChainImageViews()[i]);
                else attachments.push_back(m_Images[res.name].view);
            }
            VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO }; framebufferInfo.renderPass = graphicsPass.handle; framebufferInfo.attachmentCount = (uint32_t)attachments.size(); framebufferInfo.pAttachments = attachments.data(); framebufferInfo.width = m_Width; framebufferInfo.height = m_Height; framebufferInfo.layers = 1;
            vkCreateFramebuffer(m_Context.GetDevice(), &framebufferInfo, nullptr, &graphicsPass.framebuffers[i]);
        }
    }

    void RenderGraph::ExecuteGraphicsPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx, RenderPass& renderPass) {
        GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
        VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO }; renderPassInfo.renderPass = graphicsPass.handle; renderPassInfo.framebuffer = graphicsPass.framebuffers[imageIdx]; renderPassInfo.renderArea.offset = { 0, 0 }; renderPassInfo.renderArea.extent = { m_Width, m_Height };
        std::vector<VkClearValue> clearValues;
        for (auto& att : graphicsPass.attachments) {
            if (VulkanUtils::IsDepthFormat(att.image.format)) clearValues.push_back({ { 1.0f, 0 } });
            else clearValues.push_back(att.image.clear_value);
        }
        renderPassInfo.clearValueCount = (uint32_t)clearValues.size(); renderPassInfo.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        auto& pipeline = *m_GraphicsPipelines[renderPass.name];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);

        VkDescriptorSet globalSet = Application::Get().GetRenderState()->GetDescriptorSet(resourceIdx);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 0, 1, &globalSet, 0, nullptr);
        if (renderPass.descriptor_set != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout, 1, 1, &renderPass.descriptor_set, 0, nullptr);
        }

        ExecuteGraphicsCallback execute = [&](std::string name, GraphicsExecutionCallback cb) {
            GraphicsExecutionContext ctx(commandBuffer, m_Context, m_ResourceManager, pipeline);
            cb(ctx);
        };
        
        graphicsPass.callback(execute);
        vkCmdEndRenderPass(commandBuffer);
    }

    void RenderGraph::ExecuteRaytracingPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass) {
        RaytracingPass& rtPass = std::get<RaytracingPass>(renderPass.pass);
        
        auto& pipeline = *m_RaytracingPipelines[renderPass.name];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.handle);

        VkDescriptorSet globalSet = Application::Get().GetRenderState()->GetDescriptorSet(resourceIdx);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.layout, 0, 1, &globalSet, 0, nullptr);
        if (renderPass.descriptor_set != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.layout, 1, 1, &renderPass.descriptor_set, 0, nullptr);
        }

        ExecuteRaytracingCallback execute = [&](std::string name, RaytracingExecutionCallback cb) {
            RaytracingExecutionContext ctx(commandBuffer, m_Context, m_ResourceManager, pipeline);
            cb(ctx);
        };
        
        rtPass.callback(execute);
    }

    void RenderGraph::ExecuteComputePass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass) {
        ComputePass& computePass = std::get<ComputePass>(renderPass.pass);
        auto& pipeline = *m_ComputePipelines[renderPass.name];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);
        
        VkDescriptorSet globalSet = Application::Get().GetRenderState()->GetDescriptorSet(resourceIdx);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, 0, 1, &globalSet, 0, nullptr);
        if (renderPass.descriptor_set != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, 1, 1, &renderPass.descriptor_set, 0, nullptr);
        }

        ComputeExecutionContext ctx(commandBuffer, renderPass, *this, m_ResourceManager, resourceIdx);
        computePass.callback(ctx);
    }

    void RenderGraph::BlitImage(VkCommandBuffer commandBuffer, std::string srcImageName, std::string dstImageName, uint32_t imageIdx) {
        VkExtent2D swExtent = m_Context.GetSwapChainExtent();
        
        GraphImage srcGImg = (srcImageName == "RENDER_OUTPUT") ? GraphImage{m_Context.GetSwapChainImages()[imageIdx], m_Context.GetSwapChainImageViews()[imageIdx], VK_NULL_HANDLE, swExtent.width, swExtent.height, m_Context.GetSwapChainImageFormat()} : m_Images[srcImageName];
        GraphImage dstGImg = (dstImageName == "RENDER_OUTPUT") ? GraphImage{m_Context.GetSwapChainImages()[imageIdx], m_Context.GetSwapChainImageViews()[imageIdx], VK_NULL_HANDLE, swExtent.width, swExtent.height, m_Context.GetSwapChainImageFormat()} : m_Images[dstImageName];

        if (srcGImg.handle == VK_NULL_HANDLE || dstGImg.handle == VK_NULL_HANDLE) return;

        // Transition layouts for Blit (RenderGraph's automatic barrier might have set them to something else)
        VulkanUtils::InsertImageBarrier(commandBuffer, srcGImg.handle, 
            VulkanUtils::IsDepthFormat(srcGImg.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, VK_ACCESS_TRANSFER_READ_BIT);

        VulkanUtils::InsertImageBarrier(commandBuffer, dstGImg.handle,
            VulkanUtils::IsDepthFormat(dstGImg.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, VK_ACCESS_TRANSFER_WRITE_BIT);

        bool isDepth = VulkanUtils::IsDepthFormat(srcGImg.format);
        VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

        // Clamp blit offsets to the SMALLEST of (graph dimensions, src physical, dst physical)
        int32_t blitW = (int32_t)std::min({ m_Width, srcGImg.width, dstGImg.width });
        int32_t blitH = (int32_t)std::min({ m_Height, srcGImg.height, dstGImg.height });

        if (blitW <= 0 || blitH <= 0) return;

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { blitW, blitH, 1 };
        blit.srcSubresource = { aspect, 0, 0, 1 };
        
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { blitW, blitH, 1 };
        blit.dstSubresource = { aspect, 0, 0, 1 };

        vkCmdBlitImage(commandBuffer, srcGImg.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstGImg.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, isDepth ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);

        // Update state in RenderGraph's memory so subsequent passes (or UI) know the new layout
        if (srcImageName != "RENDER_OUTPUT") m_ImageAccess[srcImageName] = { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT };
        if (dstImageName != "RENDER_OUTPUT") m_ImageAccess[dstImageName] = { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT };
        else m_ImageAccess["RENDER_OUTPUT"] = { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT };
    }

    VkFormat RenderGraph::GetImageFormat(std::string imageName) { if (imageName == "RENDER_OUTPUT") return m_Context.GetSwapChainImageFormat(); if (m_Images.count(imageName)) return m_Images[imageName].format; return VK_FORMAT_UNDEFINED; }
    bool RenderGraph::ContainsImage(std::string imageName) { return m_Images.count(imageName) > 0; }

    std::vector<std::string> RenderGraph::GetColorAttachments() {
        std::vector<std::string> results;
        for (auto& pair : m_Images) {
            if (!VulkanUtils::IsDepthFormat(pair.second.format)) results.push_back(pair.first);
        }
        return results;
    }

    void RenderGraph::RegisterExternalResource(const std::string& name, const ImageDescription& description) {
        GraphImage img;
        img.format = description.format;
        img.is_external = true;
        m_Images[name] = img;
    }

    void RenderGraph::SetExternalResource(const std::string& name, VkImage handle, VkImageView view, VkImageLayout currentLayout, VkAccessFlags currentAccess, VkPipelineStageFlags currentStage) {
        if (!m_Images.count(name)) {
            GraphImage img;
            img.is_external = true;
            m_Images[name] = img;
        }
        auto& img = m_Images[name];
        img.handle = handle;
        img.view = view;
        m_ImageAccess[name] = { currentLayout, currentAccess, currentStage };
    }

    void RenderGraph::SetExternalResource(const std::string& name, VkImage handle, VkImageView view, VkImageLayout currentLayout, const ImageDescription& description) {
        if (!m_Images.count(name)) {
            GraphImage img;
            img.is_external = true;
            img.format = description.format;
            m_Images[name] = img;
        }
        auto& img = m_Images[name];
        img.handle = handle;
        img.view = view;
        img.format = description.format;
        m_ImageAccess[name] = { currentLayout, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
    }
}
