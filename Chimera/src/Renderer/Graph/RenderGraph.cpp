#include "pch.h"
#include "RenderGraph.h"

#include "Renderer/Backend/PipelineManager.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Utils/VulkanBarrier.h" 
#include "Utils/VulkanShaderUtils.h"
#include "GraphicsExecutionContext.h"
#include "ComputeExecutionContext.h"
#include "RaytracingExecutionContext.h"

#include <imgui.h>
#include <deque>
#include <algorithm>
#include <map>

#define VK_CHECK(x) \
	do { \
		VkResult err = x; \
		if (err) { \
			CH_CORE_ERROR("Detected Vulkan error: {}", (int)err); \
			abort(); \
		} \
	} while (0)

namespace Chimera {

	RenderGraph::RenderGraph(VulkanContext& context, ResourceManager& resourceManager, PipelineManager& pipelineManager) :
		m_Context(context),
		m_ResourceManager(resourceManager),
		m_PipelineManager(pipelineManager) {}

	RenderGraph::~RenderGraph() {
		DestroyResources();
	}

	void RenderGraph::DestroyResources() {
		VkDevice device = m_Context.GetDevice();
		vkDeviceWaitIdle(device);

		for (auto& [_, renderPass] : m_Passes) {
			if (renderPass.descriptor_set_layout != VK_NULL_HANDLE) {
				vkDestroyDescriptorSetLayout(device, renderPass.descriptor_set_layout, nullptr);
				renderPass.descriptor_set_layout = VK_NULL_HANDLE;
			}
			
			if (std::holds_alternative<GraphicsPass>(renderPass.pass)) {
				GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
				for (VkFramebuffer& framebuffer : graphicsPass.framebuffers) {
					vkDestroyFramebuffer(device, framebuffer, nullptr);
				}
				if (graphicsPass.handle != VK_NULL_HANDLE)
					vkDestroyRenderPass(device, graphicsPass.handle, nullptr);
			}
		}

		for (auto& [_, image] : m_Images) {
			if (!image.is_external)
				m_ResourceManager.DestroyGraphImage(image);
		}

		if (m_TimestampQueryPool != VK_NULL_HANDLE) {
			vkDestroyQueryPool(device, m_TimestampQueryPool, nullptr);
			m_TimestampQueryPool = VK_NULL_HANDLE;
		}

		if (m_SharedMemory != VK_NULL_HANDLE) {
			vkFreeMemory(device, m_SharedMemory, nullptr);
			m_SharedMemory = VK_NULL_HANDLE;
		}

		m_Readers.clear();
		m_Writers.clear();
		m_Passes.clear();
		m_PassDescriptions.clear();
		m_GraphicsPipelines.clear();
		m_RaytracingPipelines.clear();
		m_ComputePipelines.clear();
		m_Images.clear();
		m_ImageAccess.clear();
		m_PassTimestamps.clear();
	}

	void RenderGraph::RegisterExternalResource(const std::string& name, const ImageDescription& description) {
		GraphImage image{};
		image.width = description.width;
		image.height = description.height;
		image.format = description.format;
		image.usage = description.usage;
		image.is_external = true;
		m_Images[name] = image;
	}

	void RenderGraph::SetExternalResource(const std::string& name, VkImage handle, VkImageView view, VkImageLayout currentLayout, VkAccessFlags currentAccess, VkPipelineStageFlags currentStage) {
		assert(m_Images.count(name));
		m_Images[name].handle = handle;
		m_Images[name].view = view;
		m_ImageAccess[name] = ImageAccess{
			.layout = currentLayout,
			.access_flags = currentAccess,
			.stage_flags = currentStage
		};
	}

	void RenderGraph::AddGraphicsPass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
		std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines,
		GraphicsPassCallback callback) {
		RenderPassDescription passDescription{
			.name = renderPassName,
			.dependencies = dependencies,
			.outputs = outputs,
			.description = GraphicsPassDescription {
				.pipeline_descriptions = pipelines,
				.callback = callback
			}
		};
		assert(m_PassDescriptions.find(renderPassName) == m_PassDescriptions.end());
		m_PassDescriptions[renderPassName] = passDescription;
	}

	void RenderGraph::AddRaytracingPass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
		std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline,
		RaytracingPassCallback callback) {
		RenderPassDescription passDescription{
			.name = renderPassName,
			.dependencies = dependencies,
			.outputs = outputs,
			.description = RaytracingPassDescription {
				.pipeline_description = pipeline,
				.callback = callback
			}
		};
		assert(m_PassDescriptions.find(renderPassName) == m_PassDescriptions.end());
		m_PassDescriptions[renderPassName] = passDescription;
	}

	void RenderGraph::AddComputePass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
		std::vector<TransientResource> outputs, ComputePipelineDescription pipeline, ComputePassCallback callback) {
		RenderPassDescription passDescription{
			.name = renderPassName,
			.dependencies = dependencies,
			.outputs = outputs,
			.description = ComputePassDescription {
				.pipeline_description = pipeline,
				.callback = callback
			}
		};
		assert(m_PassDescriptions.find(renderPassName) == m_PassDescriptions.end());
		m_PassDescriptions[renderPassName] = passDescription;
	}

	void RenderGraph::AddBlitPass(const std::string& renderPassName, const std::string& srcImageName, const std::string& dstImageName) {
		VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
		if (m_Images.count(srcImageName)) format = m_Images[srcImageName].format;

		TransientResource srcRes(TransientResourceType::Image, srcImageName);
		srcRes.image.type = TransientImageType::SampledImage; 
		
		TransientResource dstRes = TransientResource::Image(dstImageName, format, 0);
		dstRes.image.type = TransientImageType::AttachmentImage; 

		RenderPassDescription passDescription{ .name = renderPassName, .dependencies = { srcRes }, .outputs = { dstRes }, .description = BlitPassDescription {} };
		m_PassDescriptions[renderPassName] = passDescription;
		RenderPass renderPass{ .name = renderPassName, .pass = BlitPass { .srcName = srcImageName, .dstName = dstImageName } };
		m_Passes[renderPassName] = renderPass;
	}

	void RenderGraph::Build() {
		for (auto& [_, passDescription] : m_PassDescriptions) {
			for (TransientResource& resource : passDescription.dependencies) m_Readers[resource.name].emplace_back(passDescription.name);
			for (TransientResource& resource : passDescription.outputs) m_Writers[resource.name].emplace_back(passDescription.name);
		}

		FindExecutionOrder();
		assert(SanityCheck());

		// --- 1. Lifetime Analysis ---
		m_ResourceLifetimes.clear();
		for (uint32_t i = 0; i < m_ExecutionOrder.size(); ++i) {
			const auto& passDesc = m_PassDescriptions[m_ExecutionOrder[i]];
			auto updateLifetime = [&](const std::string& name) {
				if (name == "RENDER_OUTPUT") return;
				if (m_ResourceLifetimes.find(name) == m_ResourceLifetimes.end()) m_ResourceLifetimes[name] = { i, i };
				else { auto& lt = m_ResourceLifetimes[name]; lt.first_pass = std::min(lt.first_pass, i); lt.last_pass = std::max(lt.last_pass, i); }
			};
			for (const auto& r : passDesc.dependencies) updateLifetime(r.name);
			for (const auto& r : passDesc.outputs) updateLifetime(r.name);
		}

		// --- 2. Memory Aliasing ---
		struct AliasingInfo { std::string name; VkMemoryRequirements reqs; uint32_t first, last; VkDeviceSize offset = 0; };
		std::vector<AliasingInfo> requests;
		for (auto& [name, lt] : m_ResourceLifetimes) {
			TransientResource* found = nullptr;
			for(auto& [_, pd] : m_PassDescriptions) {
				for(auto& r : pd.outputs) if(r.name == name) { found = &r; break; }
				if(found) break;
				for(auto& r : pd.dependencies) if(r.name == name) { found = &r; break; }
				if(found) break;
			}
			if (found && found->type == TransientResourceType::Image && found->image.format != VK_FORMAT_UNDEFINED) {
				uint32_t w = found->image.width == 0 ? m_Context.GetSwapChainExtent().width : found->image.width;
				uint32_t h = found->image.height == 0 ? m_Context.GetSwapChainExtent().height : found->image.height;
				VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				if (VulkanUtils::IsDepthFormat(found->image.format)) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
				else {
					usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
					if (found->image.type == TransientImageType::StorageImage) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
				}
				auto reqs = m_ResourceManager.GetImageMemoryRequirements(w, h, found->image.format, usage, found->image.multisampled ? m_Context.GetMSAASamples() : VK_SAMPLE_COUNT_1_BIT);
				requests.push_back({ name, reqs, lt.first_pass, lt.last_pass });
			}
		}
		std::sort(requests.begin(), requests.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

		if (!requests.empty()) {
			struct Block { VkDeviceSize offset; VkDeviceSize size; uint32_t lastUsedPass; };
			std::vector<Block> activeBlocks;
			VkDeviceSize totalPoolSize = 0;
			uint32_t memoryTypeBits = 0xFFFFFFFF;
			for (auto& req : requests) {
				memoryTypeBits &= req.reqs.memoryTypeBits;
				bool foundLoc = false;
				for (auto& b : activeBlocks) {
					if (b.lastUsedPass < req.first && b.size >= req.reqs.size) { req.offset = b.offset; b.lastUsedPass = req.last; foundLoc = true; break; }
				}
				if (!foundLoc) { req.offset = (totalPoolSize + req.reqs.alignment - 1) & ~(req.reqs.alignment - 1); totalPoolSize = req.offset + req.reqs.size; activeBlocks.push_back({ req.offset, req.reqs.size, req.last }); }
			}
			CH_CORE_INFO("Memory Aliasing: Total physical memory: {0} MB", totalPoolSize / (1024 * 1024));
			VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, totalPoolSize, m_Context.FindMemoryType(memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
			VK_CHECK(vkAllocateMemory(m_Context.GetDevice(), &allocInfo, nullptr, &m_SharedMemory));
			for (auto& req : requests) {
				TransientResource* res = nullptr;
				for(auto& [_, pd] : m_PassDescriptions) {
					for(auto& r : pd.outputs) if(r.name == req.name) { res = &r; break; }
					if(!res) for(auto& r : pd.dependencies) if(r.name == req.name) { res = &r; break; }
					if(res) break;
				}
				uint32_t w = res->image.width == 0 ? m_Context.GetSwapChainExtent().width : res->image.width;
				uint32_t h = res->image.height == 0 ? m_Context.GetSwapChainExtent().height : res->image.height;
				VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
				if (VulkanUtils::IsDepthFormat(res->image.format)) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
				else {
					usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
					if (res->image.type == TransientImageType::StorageImage) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
				}
				m_Images[req.name] = m_ResourceManager.CreateImageAliased(w, h, res->image.format, usage, res->image.multisampled ? m_Context.GetMSAASamples() : VK_SAMPLE_COUNT_1_BIT, m_SharedMemory, req.offset);
				m_ImageAccess[req.name] = { VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
			}
		}

		// --- 3. Pass & Pipeline Creation ---
		for (auto& [_, passDescription] : m_PassDescriptions) {
			if (std::holds_alternative<GraphicsPassDescription>(passDescription.description)) CreateGraphicsPass(passDescription);
			else if (std::holds_alternative<RaytracingPassDescription>(passDescription.description)) CreateRaytracingPass(passDescription);
			else if (std::holds_alternative<ComputePassDescription>(passDescription.description)) CreateComputePass(passDescription);
		}

		VkQueryPoolCreateInfo queryPoolInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, nullptr, 0, VK_QUERY_TYPE_TIMESTAMP, (uint32_t)m_ExecutionOrder.size() * 2 };
		VK_CHECK(vkCreateQueryPool(m_Context.GetDevice(), &queryPoolInfo, nullptr, &m_TimestampQueryPool));
	}

	void RenderGraph::Execute(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx, std::function<void(VkCommandBuffer)> uiDrawCallback) {
		if (m_ExecutionOrder.empty()) {
			if (uiDrawCallback) uiDrawCallback(commandBuffer);
			return;
		}

		// Initial Clear of Swapchain to prevent ghosting
		VkImage swapImage = m_Context.GetSwapChainImages()[imageIdx];
		VulkanUtils::InsertImageBarrier(commandBuffer, swapImage, 
			VK_IMAGE_ASPECT_COLOR_BIT, 
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, VK_ACCESS_TRANSFER_WRITE_BIT);
		
		VkClearColorValue clearColor = { 0.1f, 0.1f, 0.1f, 1.0f };
		VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		vkCmdClearColorImage(commandBuffer, swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

		VulkanUtils::InsertImageBarrier(commandBuffer, swapImage, 
			VK_IMAGE_ASPECT_COLOR_BIT, 
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

		if (m_TimestampQueryPool != VK_NULL_HANDLE) {
			vkCmdResetQueryPool(commandBuffer, m_TimestampQueryPool, 0, (uint32_t)m_ExecutionOrder.size() * 2);
		}

		m_ImageAccess["RENDER_OUTPUT"] = { VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };
		for (int i = 0; i < (int)m_ExecutionOrder.size(); ++i) {
			std::string& passName = m_ExecutionOrder[i];
			if (m_Passes.find(passName) == m_Passes.end()) {
				CH_CORE_ERROR("RenderGraph: Pass '{0}' not found in executable passes!", passName);
				continue;
			}
			RenderPass& renderPass = m_Passes[passName];
			
			// Use vkCmdBeginDebugUtilsLabelEXT only if pointers are available (handled by volk typically)
			VkDebugUtilsLabelEXT passLabel{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, nullptr, renderPass.name.c_str() };
			if (vkCmdBeginDebugUtilsLabelEXT) vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &passLabel);

			if (std::holds_alternative<GraphicsPass>(renderPass.pass)) {
				if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, m_TimestampQueryPool, (i * 2));
				InsertBarriers(commandBuffer, renderPass, imageIdx);
				ExecuteGraphicsPass(commandBuffer, resourceIdx, imageIdx, renderPass);
				if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, m_TimestampQueryPool, (i * 2) + 1);
			} else if (std::holds_alternative<RaytracingPass>(renderPass.pass)) {
				if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, m_TimestampQueryPool, (i * 2));
				InsertBarriers(commandBuffer, renderPass, imageIdx);
				ExecuteRaytracingPass(commandBuffer, resourceIdx, renderPass);
				if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, m_TimestampQueryPool, (i * 2) + 1);
			} else if (std::holds_alternative<ComputePass>(renderPass.pass)) {
				if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_TimestampQueryPool, (i * 2));
				InsertBarriers(commandBuffer, renderPass, imageIdx);
				ExecuteComputePass(commandBuffer, resourceIdx, renderPass);
				if (m_TimestampQueryPool != VK_NULL_HANDLE) vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_TimestampQueryPool, (i * 2) + 1);
			} else if (std::holds_alternative<BlitPass>(renderPass.pass)) {
				InsertBarriers(commandBuffer, renderPass, imageIdx);
				BlitPass& blit = std::get<BlitPass>(renderPass.pass);
				
				if (m_Images.find(blit.srcName) == m_Images.end()) {
					CH_CORE_ERROR("RenderGraph: Blit source '{0}' not found!", blit.srcName);
					continue;
				}

				VkImage srcImg = m_Images[blit.srcName].handle;
				VkImage dstImg = (blit.dstName == "RENDER_OUTPUT") ? m_Context.GetSwapChainImages()[imageIdx] : m_Images[blit.dstName].handle;
				
				uint32_t srcW = m_Images[blit.srcName].width;
				uint32_t srcH = m_Images[blit.srcName].height;
				uint32_t dstW = (blit.dstName == "RENDER_OUTPUT") ? m_Context.GetSwapChainExtent().width : m_Images[blit.dstName].width;
				uint32_t dstH = (blit.dstName == "RENDER_OUTPUT") ? m_Context.GetSwapChainExtent().height : m_Images[blit.dstName].height;

				if (srcImg != VK_NULL_HANDLE && dstImg != VK_NULL_HANDLE) {
					VkImageBlit blitRegion{};
					blitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
					blitRegion.srcOffsets[0] = { 0, 0, 0 };
					blitRegion.srcOffsets[1] = { (int32_t)srcW, (int32_t)srcH, 1 };
					
					blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
					blitRegion.dstOffsets[0] = { 0, 0, 0 };
					blitRegion.dstOffsets[1] = { (int32_t)dstW, (int32_t)dstH, 1 };

					vkCmdBlitImage(
						commandBuffer,
						srcImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						dstImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						1, &blitRegion,
						VK_FILTER_LINEAR
					);
					
					if (blit.dstName == "RENDER_OUTPUT") {
						// Final transition to COLOR_ATTACHMENT_OPTIMAL so ImGui can draw over it
						VulkanUtils::InsertImageBarrier(commandBuffer, dstImg,
							VK_IMAGE_ASPECT_COLOR_BIT,
							VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
							VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
							VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
						
						// Don't persist RENDER_OUTPUT in m_ImageAccess as it changes per frame (imageIdx)
					}
				}
			}
			if (vkCmdEndDebugUtilsLabelEXT) vkCmdEndDebugUtilsLabelEXT(commandBuffer);
		}
		if (uiDrawCallback) uiDrawCallback(commandBuffer);
	}

	void RenderGraph::GatherPerformanceStatistics() {
		uint32_t timestampCount = static_cast<uint32_t>(m_ExecutionOrder.size()) * 2;
		std::vector<uint64_t> timestamps(timestampCount);
		vkGetQueryPoolResults(m_Context.GetDevice(), m_TimestampQueryPool, 0, timestampCount, timestampCount * sizeof(uint64_t), timestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
		float timestampPeriod = m_Context.GetDeviceProperties().limits.timestampPeriod;
		for (int i = 0; i < m_ExecutionOrder.size(); ++i) {
			std::string& passName = m_ExecutionOrder[i];
			double t1 = static_cast<double>(timestamps[(i * 2)]) * timestampPeriod * 1e-6;
			double t2 = static_cast<double>(timestamps[(i * 2) + 1]) * timestampPeriod * 1e-6;
			m_PassTimestamps[passName] = m_PassTimestamps[passName] * 0.95 + (t2 - t1) * 0.05;
		}
	}

	void RenderGraph::DrawPerformanceStatistics() {
		size_t strlen = 0;
		for (std::string& passName : m_ExecutionOrder) if (passName.length() > strlen) strlen = passName.length();
		ImGuiIO& io = ImGui::GetIO(); ImGui::Begin("Performance Statistics"); ImGui::Text("FPS: %s%f", std::string(strlen > 3 ? strlen - 3 : 0, ' ').c_str(), io.Framerate);
		for (std::string& passName : m_ExecutionOrder) ImGui::Text("%s: %s%fms", passName.c_str(), std::string(strlen - passName.length(), ' ').c_str(), m_PassTimestamps[passName]);
		ImGui::End();
	}

	void RenderGraph::CopyImage(VkCommandBuffer commandBuffer, std::string srcImageName, Image& dstImage) {
		GraphImage& srcImage = m_Images[srcImageName]; ImageAccess currentAccess = m_ImageAccess[srcImageName];
		if (currentAccess.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) VulkanUtils::InsertImageBarrier(commandBuffer, srcImage.handle, VK_IMAGE_ASPECT_COLOR_BIT, currentAccess.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, currentAccess.stage_flags, VK_PIPELINE_STAGE_TRANSFER_BIT, currentAccess.access_flags, VK_ACCESS_TRANSFER_READ_BIT);
		m_ImageAccess[srcImageName] = { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
		VulkanUtils::InsertImageBarrier(commandBuffer, dstImage.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
		VkImageCopy imageCopy{ {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {srcImage.width, srcImage.height, 1} };
		vkCmdCopyImage(commandBuffer, srcImage.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);
		VulkanUtils::InsertImageBarrier(commandBuffer, dstImage.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

    void RenderGraph::CopyImage(VkCommandBuffer commandBuffer, std::string srcImageName, GraphImage& dstImage) {
        GraphImage& src = m_Images[srcImageName]; ImageAccess current = m_ImageAccess[srcImageName];
        if (current.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) VulkanUtils::InsertImageBarrier(commandBuffer, src.handle, VK_IMAGE_ASPECT_COLOR_BIT, current.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, current.stage_flags, VK_PIPELINE_STAGE_TRANSFER_BIT, current.access_flags, VK_ACCESS_TRANSFER_READ_BIT);
        m_ImageAccess[srcImageName] = { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
        VulkanUtils::InsertImageBarrier(commandBuffer, dstImage.handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageCopy imageCopy{ {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {src.width, src.height, 1} };
        vkCmdCopyImage(commandBuffer, src.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);
        VulkanUtils::InsertImageBarrier(commandBuffer, dstImage.handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

	void RenderGraph::BlitImage(VkCommandBuffer commandBuffer, std::string srcImageName, std::string dstImageName) {
		GraphImage& src = m_Images[srcImageName]; VulkanUtils::InsertImageBarrier(commandBuffer, src.handle, VK_IMAGE_ASPECT_COLOR_BIT, m_ImageAccess[srcImageName].layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_ImageAccess[srcImageName].stage_flags, VK_PIPELINE_STAGE_TRANSFER_BIT, m_ImageAccess[srcImageName].access_flags, VK_ACCESS_TRANSFER_READ_BIT);
	}

	bool RenderGraph::ContainsImage(std::string imageName) { return m_Images.count(imageName); }
	VkFormat RenderGraph::GetImageFormat(std::string imageName) { return m_Images[imageName].format; }

	std::vector<std::string> RenderGraph::GetColorAttachments() {
		std::vector<std::string> colorAttachmentNames;
		for (auto& [name, image] : m_Images) if (!VulkanUtils::IsDepthFormat(image.format) && !name.ends_with("_MSAA")) colorAttachmentNames.emplace_back(name);
		return colorAttachmentNames;
	}

	void RenderGraph::CreatePassDescriptorSet(RenderPass& renderPass, RenderPassDescription& passDescription, VkShaderStageFlags stageFlags) {
		std::vector<VkDescriptorSetLayoutBinding> bindings; std::vector<VkDescriptorImageInfo> imgInfos; std::vector<VkDescriptorBufferInfo> bufInfos; std::vector<VkWriteDescriptorSetAccelerationStructureKHR> asInfos;
		auto processResource = [&](TransientResource& res) {
			if (res.type == TransientResourceType::Image && res.image.type != TransientImageType::AttachmentImage) {
				VkDescriptorType type = (res.image.type == TransientImageType::StorageImage) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				if (res.image.descriptor_type_override != VK_DESCRIPTOR_TYPE_MAX_ENUM) type = res.image.descriptor_type_override;
				imgInfos.push_back({ (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) ? VK_NULL_HANDLE : m_ResourceManager.GetDefaultSampler(), m_Images[res.name].view, (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
				bindings.push_back({ res.image.binding, type, 1, stageFlags, nullptr });
			} else if (res.type == TransientResourceType::Buffer) {
				bufInfos.push_back({ res.buffer.handle, 0, VK_WHOLE_SIZE });
				bindings.push_back({ res.buffer.binding, res.buffer.descriptor_type, 1, stageFlags, nullptr });
			} else if (res.type == TransientResourceType::AccelerationStructure) {
				asInfos.push_back({ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, nullptr, 1, &res.as.handle });
				bindings.push_back({ res.as.binding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, stageFlags, nullptr });
			}
		};
		for (auto& r : passDescription.dependencies) processResource(r);
		for (auto& r : passDescription.outputs) processResource(r);
		if (bindings.empty()) return;
		VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, (uint32_t)bindings.size(), bindings.data() };
		VK_CHECK(vkCreateDescriptorSetLayout(m_Context.GetDevice(), &layoutInfo, nullptr, &renderPass.descriptor_set_layout));
		VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_ResourceManager.GetTransientDescriptorPool(), 1, &renderPass.descriptor_set_layout };
		VK_CHECK(vkAllocateDescriptorSets(m_Context.GetDevice(), &allocInfo, &renderPass.descriptor_set));
		CH_CORE_INFO("RenderGraph: Allocated descriptor set for pass '{0}'", renderPass.name);
		if (renderPass.descriptor_set == VK_NULL_HANDLE) return;
		std::vector<VkWriteDescriptorSet> writes; int iIdx = 0, bIdx = 0, aIdx = 0;
		for (auto& b : bindings) {
			VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, renderPass.descriptor_set, b.binding, 0, 1, b.descriptorType };
			if (b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || b.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) w.pImageInfo = &imgInfos[iIdx++];
			else if (b.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) w.pBufferInfo = &bufInfos[bIdx++];
			else if (b.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR) w.pNext = &asInfos[aIdx++];
			writes.push_back(w);
		}
		vkUpdateDescriptorSets(m_Context.GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
	}

	void RenderGraph::ParseGraphicsAttachments(RenderPassDescription& passDescription, GraphicsPass& graphicsPass, std::vector<VkAttachmentDescription>& attachments, std::vector<VkAttachmentReference>& colorRefs, VkAttachmentReference& depthRef, bool& isMultisampled) {
		uint32_t colorCount = 0; uint32_t totalCount = 0; isMultisampled = false;
		for (TransientResource& output : passDescription.outputs) if (output.type == TransientResourceType::Image && output.image.type == TransientImageType::AttachmentImage) { if (!VulkanUtils::IsDepthFormat(output.image.format)) colorCount++; if (output.image.multisampled) isMultisampled = true; totalCount++; }
		attachments.resize(totalCount); colorRefs.resize(colorCount); graphicsPass.attachments.resize(totalCount);
		for (TransientResource& output : passDescription.outputs) {
			if (output.type != TransientResourceType::Image || output.image.type != TransientImageType::AttachmentImage) continue;
			uint32_t binding = output.image.binding; bool isRenderOutput = (output.name == "RENDER_OUTPUT"); VkImageLayout layout = VulkanUtils::GetImageLayoutFromResourceType(output.image.type, output.image.format);
			graphicsPass.attachments[binding] = output;
			attachments[binding] = { 0, isRenderOutput ? m_Context.GetSwapChainImageFormat() : output.image.format, output.image.multisampled ? m_Context.GetMSAASamples() : VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, isRenderOutput ? (output.image.multisampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) : layout };
			if (VulkanUtils::IsDepthFormat(output.image.format)) depthRef = { binding, layout }; else colorRefs[binding] = { binding, layout };
		}
	}

	void RenderGraph::CreateGraphicsPass(RenderPassDescription& passDescription) {
		GraphicsPassDescription& graphicsPassDescription = std::get<GraphicsPassDescription>(passDescription.description);
		RenderPass renderPass{ .name = passDescription.name, .pass = GraphicsPass { .callback = graphicsPassDescription.callback } };
		GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
		std::vector<VkAttachmentDescription> attachments; std::vector<VkAttachmentReference> colorRefs; VkAttachmentReference depthRef{ VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED }; bool isMultisampled = false;
		ParseGraphicsAttachments(passDescription, graphicsPass, attachments, colorRefs, depthRef, isMultisampled);
		VkSubpassDescription subpass{ 0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, (uint32_t)colorRefs.size(), colorRefs.data(), nullptr, (depthRef.attachment == VK_ATTACHMENT_UNUSED) ? nullptr : &depthRef, 0, nullptr };
		VkSubpassDependency dependency{ VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0 };
		VkRenderPassCreateInfo rpInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, (uint32_t)attachments.size(), attachments.data(), 1, &subpass, 1, &dependency };
		VK_CHECK(vkCreateRenderPass(m_Context.GetDevice(), &rpInfo, nullptr, &graphicsPass.handle));
		CreatePassDescriptorSet(renderPass, passDescription, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
		for (auto& pd : graphicsPassDescription.pipeline_descriptions) m_GraphicsPipelines[pd.name] = &m_PipelineManager.GetGraphicsPipeline(renderPass, pd);
		CreateFramebuffers(renderPass); m_Passes[renderPass.name] = renderPass;
	}

	void RenderGraph::CreateRaytracingPass(RenderPassDescription& passDescription) {
		RaytracingPassDescription& rtDesc = std::get<RaytracingPassDescription>(passDescription.description);
		RenderPass renderPass{ .name = passDescription.name, .pass = RaytracingPass { .callback = rtDesc.callback } };
		CreatePassDescriptorSet(renderPass, passDescription, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR);
		m_RaytracingPipelines[rtDesc.pipeline_description.name] = &m_PipelineManager.GetRaytracingPipeline(renderPass, rtDesc.pipeline_description);
		m_Passes[renderPass.name] = renderPass;
	}

	void RenderGraph::CreateComputePass(RenderPassDescription& passDescription) {
		ComputePassDescription& compDesc = std::get<ComputePassDescription>(passDescription.description);
		RenderPass renderPass{ .name = passDescription.name, .pass = ComputePass { .callback = compDesc.callback } };
		CreatePassDescriptorSet(renderPass, passDescription, VK_SHADER_STAGE_COMPUTE_BIT);
		for (auto& kernel : compDesc.pipeline_description.kernels) m_ComputePipelines[kernel.shader] = &m_PipelineManager.GetComputePipeline(renderPass, compDesc.pipeline_description.push_constant_description, kernel);
		m_Passes[renderPass.name] = renderPass;
	}

	void RenderGraph::CreateFramebuffers(RenderPass& renderPass) {
		if (!std::holds_alternative<GraphicsPass>(renderPass.pass)) return;
		GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
		bool writesToRenderOutput = false;
		for (auto& attachment : graphicsPass.attachments) { if (attachment.name == "RENDER_OUTPUT") { writesToRenderOutput = true; break; } }
		uint32_t framebufferCount = writesToRenderOutput ? m_Context.GetSwapChainImageCount() : 1;
		graphicsPass.framebuffers.resize(framebufferCount);
		for (uint32_t i = 0; i < framebufferCount; i++) {
			std::vector<VkImageView> imageViews; bool isMultisampledPass = false;
			for (TransientResource& attachment : graphicsPass.attachments) {
				if (attachment.name == "RENDER_OUTPUT") {
					if (attachment.image.multisampled) { imageViews.emplace_back(m_Images[renderPass.name + "_MSAA"].view); isMultisampledPass = true; }
					else imageViews.emplace_back(m_Context.GetSwapChainImageViews()[i]);
				} else imageViews.emplace_back(m_Images[attachment.name].view);
			}
			if (isMultisampledPass) imageViews.emplace_back(m_Context.GetSwapChainImageViews()[i]);
			uint32_t passWidth = graphicsPass.attachments[0].image.width; uint32_t passHeight = graphicsPass.attachments[0].image.height;
			if (passWidth == 0 || passHeight == 0) { passWidth = m_Context.GetSwapChainExtent().width; passHeight = m_Context.GetSwapChainExtent().height; }
			VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, graphicsPass.handle, (uint32_t)imageViews.size(), imageViews.data(), passWidth, passHeight, 1 };
			VK_CHECK(vkCreateFramebuffer(m_Context.GetDevice(), &framebufferInfo, nullptr, &graphicsPass.framebuffers[i]));
		}
	}

	void RenderGraph::FindExecutionOrder() {
		// 优先级：Viewport 需要的 FinalColor 优先作为根节�?
		std::string finalTarget = "";
		if (m_Writers.find("FinalColor") != m_Writers.end()) {
			finalTarget = "FinalColor";
		} else if (m_Writers.find("RENDER_OUTPUT") != m_Writers.end()) {
			finalTarget = "RENDER_OUTPUT";
		}

		if (finalTarget.empty()) {
			CH_CORE_WARN("RenderGraph: No root resource (FinalColor or RENDER_OUTPUT) found to start traversal!");
			return;
		}

		CH_CORE_INFO("RenderGraph: Starting traversal from root: {0}", finalTarget);
		
		m_ExecutionOrder.clear();
		std::deque<std::string> stack;
		// 注意：一个资源可能被多个 Pass 写入（虽然目前我们不支持，但为了健壮性）
		for (const auto& passName : m_Writers[finalTarget]) {
			m_ExecutionOrder.push_back(passName);
			stack.push_back(passName);
		}

		while (!stack.empty()) {
			std::string passName = stack.front();
			stack.pop_front();

			if (m_PassDescriptions.count(passName)) {
				for (const auto& dependency : m_PassDescriptions[passName].dependencies) {
					if (m_Writers.count(dependency.name)) {
						for (const auto& writerPass : m_Writers[dependency.name]) {
							// 避免重复添加
							if (std::find(m_ExecutionOrder.begin(), m_ExecutionOrder.end(), writerPass) == m_ExecutionOrder.end()) {
								m_ExecutionOrder.push_back(writerPass);
								stack.push_back(writerPass);
							}
						}
					}
				}
			}
		}
		
		std::reverse(m_ExecutionOrder.begin(), m_ExecutionOrder.end());
		
		// 去重保持顺序
		std::vector<std::string> uniqueOrder;
		for (const auto& name : m_ExecutionOrder) {
			if (std::find(uniqueOrder.begin(), uniqueOrder.end(), name) == uniqueOrder.end()) {
				uniqueOrder.push_back(name);
			}
		}
		m_ExecutionOrder = uniqueOrder;

		CH_CORE_INFO("RenderGraph: Final Execution Order:");
		for(const auto& pass : m_ExecutionOrder) CH_CORE_INFO("  [Pass] {0}", pass);
	}

	void RenderGraph::InsertBarriers(VkCommandBuffer commandBuffer, RenderPass& renderPass, uint32_t imageIdx) {
		RenderPassDescription& passDescription = m_PassDescriptions[renderPass.name];
		bool isGraphicsPass = std::holds_alternative<GraphicsPass>(renderPass.pass);
		bool isComputePass = std::holds_alternative<ComputePass>(renderPass.pass);
		bool isRaytracingPass = std::holds_alternative<RaytracingPass>(renderPass.pass);
		bool isBlitPass = std::holds_alternative<BlitPass>(renderPass.pass);

		auto getRequiredLayout = [&](const TransientResource& resource, bool isOutput) {
			if (isOutput) {
				if (VulkanUtils::IsDepthFormat(resource.image.format)) return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
			
			switch (resource.type) {
				case TransientResourceType::Sampler: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				case TransientResourceType::Storage: return VK_IMAGE_LAYOUT_GENERAL;
				default: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
		};

		auto processResource = [&](const TransientResource& resource, bool isOutput) {
			if (resource.type != TransientResourceType::Image) return;
			
			ImageAccess currentAccess;
			VkImage imgHandle = VK_NULL_HANDLE;

			if (resource.name == "RENDER_OUTPUT") {
				currentAccess = { VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
				imgHandle = m_Context.GetSwapChainImages()[imageIdx];
			} else {
				if (m_ImageAccess.find(resource.name) == m_ImageAccess.end()) return;
				currentAccess = m_ImageAccess[resource.name];
				imgHandle = m_Images[resource.name].handle;
			}

			VkImageLayout dstLayout = getRequiredLayout(resource, isOutput);
			
			if (isBlitPass) {
				BlitPass& blit = std::get<BlitPass>(renderPass.pass);
				if (resource.name == blit.srcName) dstLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				else if (resource.name == blit.dstName) dstLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			}

			if (currentAccess.layout != dstLayout) {
				VkImageAspectFlags aspectFlags = VulkanUtils::IsDepthFormat(resource.image.format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
				
				// Stage/Access prediction
				VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
				VkAccessFlags dstAccess = isOutput ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;

				if (isGraphicsPass) {
					dstStage = isOutput ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
					if (isOutput) dstAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				} else if (isRaytracingPass) {
					dstStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
				} else if (isComputePass) {
					dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
				}

				if (dstLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) { dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT; dstAccess = VK_ACCESS_TRANSFER_READ_BIT; }
				if (dstLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) { dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT; dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT; }

				VulkanUtils::InsertImageBarrier(commandBuffer, imgHandle, aspectFlags, 
					currentAccess.layout, dstLayout, 
					currentAccess.stage_flags, dstStage, 
					currentAccess.access_flags, dstAccess);

				m_ImageAccess[resource.name] = { dstLayout, dstAccess, dstStage };
			}
		};

		if (isBlitPass) {
			BlitPass& blit = std::get<BlitPass>(renderPass.pass);
			TransientResource srcRes; srcRes.name = blit.srcName; srcRes.type = TransientResourceType::Image;
			TransientResource dstRes; dstRes.name = blit.dstName; dstRes.type = TransientResourceType::Image;
			processResource(srcRes, false);
			processResource(dstRes, true);
		} else {
			for (auto& res : passDescription.dependencies) processResource(res, false);
			for (auto& res : passDescription.outputs) processResource(res, true);
		}
	}

	void RenderGraph::ExecuteGraphicsPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx, RenderPass& renderPass) {
		GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
		bool writesToRenderOutput = false;
		for (auto& attachment : graphicsPass.attachments) { if (attachment.name == "RENDER_OUTPUT") { writesToRenderOutput = true; break; } }
		uint32_t fbIdx = writesToRenderOutput ? imageIdx : 0;
		VkFramebuffer framebuffer = graphicsPass.framebuffers[fbIdx];
		uint32_t passWidth = graphicsPass.attachments[0].image.width; uint32_t passHeight = graphicsPass.attachments[0].image.height;
		if (passWidth == 0 || passHeight == 0) { passWidth = m_Context.GetSwapChainExtent().width; passHeight = m_Context.GetSwapChainExtent().height; }
		VkRenderPassBeginInfo renderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, graphicsPass.handle, framebuffer, { {0, 0}, {passWidth, passHeight} } };
		std::vector<VkClearValue> clearValues;
		for (TransientResource& attachment : graphicsPass.attachments) clearValues.emplace_back(attachment.image.clear_value);
		renderPassBeginInfo.clearValueCount = (uint32_t)clearValues.size(); renderPassBeginInfo.pClearValues = clearValues.data();
		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
		graphicsPass.callback([&](std::string pipelineName, GraphicsExecutionCallback executePipeline) {
			GraphicsPipeline* pipeline = m_GraphicsPipelines[pipelineName];
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
			GraphicsExecutionContext executionContext(commandBuffer, m_Context, m_ResourceManager, *pipeline);
			executionContext.BindGlobalSet(0, resourceIdx);
			if (renderPass.descriptor_set != VK_NULL_HANDLE) executionContext.BindPassSet(1, renderPass.descriptor_set);
			executePipeline(executionContext);
		});
		vkCmdEndRenderPass(commandBuffer);
	}

	void RenderGraph::ExecuteRaytracingPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass) {
		RaytracingPass& raytracingPass = std::get<RaytracingPass>(renderPass.pass);
		raytracingPass.callback([&](std::string pipelineName, RaytracingExecutionCallback executePipeline) {
			RaytracingPipeline* pipeline = m_RaytracingPipelines[pipelineName];
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->handle);
			RaytracingExecutionContext executionContext(commandBuffer, m_Context, m_ResourceManager, *pipeline);
			executionContext.BindGlobalSet(0, resourceIdx);
			if (renderPass.descriptor_set != VK_NULL_HANDLE) executionContext.BindPassSet(1, renderPass.descriptor_set);
			executePipeline(executionContext);
		});
	}

	void RenderGraph::ExecuteComputePass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass) {
		ComputePass& computePass = std::get<ComputePass>(renderPass.pass);
		ComputeExecutionContext executionContext(commandBuffer, renderPass, *this, m_ResourceManager, resourceIdx);
		computePass.callback(executionContext);
	}

	bool RenderGraph::SanityCheck() { return true; } 
}
