#include "pch.h"
#include "RenderGraph.h"

#include "gfx/pipeline/Pipeline.h"
#include "gfx/resources/ResourceManager.h"
#include "gfx/vulkan/VulkanContext.h"
#include "gfx/utils/VulkanBarrier.h" 
#include "gfx/utils/VulkanShaderUtils.h"
#include "GraphicsExecutionContext.h"
#include "ComputeExecutionContext.h"
#include "RaytracingExecutionContext.h"

#include <imgui.h>
#include <deque>
#include <algorithm>

#define VK_CHECK(x) \
	do { \
		VkResult err = x; \
		if (err) { \
			CH_CORE_ERROR("Detected Vulkan error: {}", (int)err); \
			abort(); \
		} \
	} while (0)

namespace Chimera {

	RenderGraph::RenderGraph(VulkanContext& context, ResourceManager& resourceManager) :
		m_Context(context),
		m_ResourceManager(resourceManager) {}

	RenderGraph::~RenderGraph() {
		DestroyResources();
	}

	void RenderGraph::DestroyResources() {
		VkDevice device = m_Context.GetDevice();
		vkDeviceWaitIdle(device);

		for (auto& [_, renderPass] : m_Passes) {
			vkDestroyDescriptorSetLayout(device, renderPass.descriptor_set_layout, nullptr);
			if (std::holds_alternative<GraphicsPass>(renderPass.pass)) {
				GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);
				for (VkFramebuffer& framebuffer : graphicsPass.framebuffers) {
					vkDestroyFramebuffer(device, framebuffer, nullptr);
				}
				vkDestroyRenderPass(device, graphicsPass.handle, nullptr);
			}
		}

		for (auto& [_, pipeline] : m_GraphicsPipelines) {
			vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
			vkDestroyPipeline(device, pipeline.handle, nullptr);
		}

		for (auto& [_, pipeline] : m_ComputePipelines) {
			vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
			vkDestroyPipeline(device, pipeline.handle, nullptr);
		}

		for (auto& [_, pipeline] : m_RaytracingPipelines) {
			vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
			vkDestroyPipeline(device, pipeline.handle, nullptr);
		}

		for (auto& [_, image] : m_Images) {
			if (!image.is_external)
				m_ResourceManager.DestroyGraphImage(image);
		}

		if (m_TimestampQueryPool != VK_NULL_HANDLE) {
			vkDestroyQueryPool(device, m_TimestampQueryPool, nullptr);
			m_TimestampQueryPool = VK_NULL_HANDLE;
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

	void RenderGraph::AddGraphicsPass(const char* renderPassName, std::vector<TransientResource> dependencies,
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

	void RenderGraph::AddRaytracingPass(const char* renderPassName, std::vector<TransientResource> dependencies,
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

	void RenderGraph::AddComputePass(const char* renderPassName, std::vector<TransientResource> dependencies,
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

	void RenderGraph::Build() {
		for (auto& [_, passDescription] : m_PassDescriptions) {
			for (TransientResource& resource : passDescription.dependencies) {
				m_Readers[resource.name].emplace_back(passDescription.name);
				ActualizeResource(resource, passDescription.name);
			}
			for (TransientResource& resource : passDescription.outputs) {
				m_Writers[resource.name].emplace_back(passDescription.name);
				ActualizeResource(resource, passDescription.name);
			}
			if (std::holds_alternative<GraphicsPassDescription>(passDescription.description)) {
				CreateGraphicsPass(passDescription);
			}
			else if (std::holds_alternative<RaytracingPassDescription>(passDescription.description)) {
				CreateRaytracingPass(passDescription);
			}
			else if (std::holds_alternative<ComputePassDescription>(passDescription.description)) {
				CreateComputePass(passDescription);
			}
		}

		FindExecutionOrder();
		assert(SanityCheck());

		VkQueryPoolCreateInfo queryPoolInfo{
			.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
			.queryType = VK_QUERY_TYPE_TIMESTAMP,
			.queryCount = static_cast<uint32_t>(m_ExecutionOrder.size()) * 2
		};
		VK_CHECK(vkCreateQueryPool(m_Context.GetDevice(), &queryPoolInfo, nullptr, &m_TimestampQueryPool));
	}

	void RenderGraph::Execute(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx) {
		uint32_t timestampCount = static_cast<uint32_t>(m_ExecutionOrder.size()) * 2;
		vkCmdResetQueryPool(commandBuffer, m_TimestampQueryPool, 0, timestampCount);

		for (int i = 0; i < m_ExecutionOrder.size(); ++i) {
			std::string& passName = m_ExecutionOrder[i];
			assert(m_Passes.find(passName) != m_Passes.end());
			RenderPass& renderPass = m_Passes[passName];

			VkDebugUtilsLabelEXT passLabel{
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
				.pLabelName = renderPass.name
			};
			vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &passLabel);

			if (std::holds_alternative<GraphicsPass>(renderPass.pass)) {
				vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, m_TimestampQueryPool, (i * 2));
				InsertBarriers(commandBuffer, renderPass);
				ExecuteGraphicsPass(commandBuffer, resourceIdx, imageIdx, renderPass);
				vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, m_TimestampQueryPool, (i * 2) + 1);
			}
			else if (std::holds_alternative<RaytracingPass>(renderPass.pass)) {
				vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, m_TimestampQueryPool, (i * 2));
				InsertBarriers(commandBuffer, renderPass);
				ExecuteRaytracingPass(commandBuffer, resourceIdx, renderPass);
				vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, m_TimestampQueryPool, (i * 2) + 1);
			}
			else if (std::holds_alternative<ComputePass>(renderPass.pass)) {
				vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_TimestampQueryPool, (i * 2));
				InsertBarriers(commandBuffer, renderPass);
				ExecuteComputePass(commandBuffer, resourceIdx, renderPass);
				vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_TimestampQueryPool, (i * 2) + 1);
			}

			vkCmdEndDebugUtilsLabelEXT(commandBuffer);
		}
	}

	void RenderGraph::GatherPerformanceStatistics() {
		uint32_t timestampCount = static_cast<uint32_t>(m_ExecutionOrder.size()) * 2;
		std::vector<uint64_t> timestamps(timestampCount);
		VK_CHECK(vkGetQueryPoolResults(m_Context.GetDevice(), m_TimestampQueryPool, 0, timestampCount,
			timestampCount * sizeof(uint64_t), timestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));

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
		for (std::string& passName : m_ExecutionOrder) {
			if (passName.length() > strlen) {
				strlen = passName.length();
			}
		}

		ImGuiIO& io = ImGui::GetIO();
		ImGui::Begin("Performance Statistics");
		ImGui::Text("FPS: %s%f", std::string(strlen > 3 ? strlen - 3 : 0, ' ').c_str(), io.Framerate);

		for (std::string& passName : m_ExecutionOrder) {
			ImGui::Text("%s: %s%fms", passName.c_str(), std::string(strlen - passName.length(), ' ').c_str(), m_PassTimestamps[passName]);
		}

		ImGui::End();
	}

	void RenderGraph::CopyImage(VkCommandBuffer commandBuffer, std::string srcImageName, Image& dstImage) {
		assert(m_Images.count(srcImageName));
		assert(m_ImageAccess.count(srcImageName));
		GraphImage& srcImage = m_Images[srcImageName];
		ImageAccess currentAccess = m_ImageAccess[srcImageName];

		if (m_ImageAccess[srcImageName].layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			VulkanUtils::InsertImageBarrier(
				commandBuffer, srcImage.handle, VK_IMAGE_ASPECT_COLOR_BIT,
				currentAccess.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, currentAccess.stage_flags,
				VK_PIPELINE_STAGE_TRANSFER_BIT, currentAccess.access_flags, VK_ACCESS_TRANSFER_READ_BIT
			);
		}
		m_ImageAccess[srcImageName] = ImageAccess{
			.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.access_flags = VK_ACCESS_TRANSFER_READ_BIT,
			.stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT
		};

		VulkanUtils::InsertImageBarrier(commandBuffer, dstImage.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, VK_ACCESS_TRANSFER_WRITE_BIT);

		VkImageCopy imageCopy{
			.srcSubresource = VkImageSubresourceLayers {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.layerCount = 1
			},
			.dstSubresource = VkImageSubresourceLayers {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.layerCount = 1
			},
			.extent = VkExtent3D {
				.width = srcImage.width,
				.height = srcImage.height,
				.depth = 1
			}
		};
		vkCmdCopyImage(
			commandBuffer,
			srcImage.handle,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&imageCopy
		);

		VulkanUtils::InsertImageBarrier(commandBuffer, dstImage.GetImage(), VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	void RenderGraph::CopyImage(VkCommandBuffer commandBuffer, std::string srcImageName, GraphImage& dstImage) {
		assert(m_Images.count(srcImageName));
		assert(m_ImageAccess.count(srcImageName));
		GraphImage& srcImage = m_Images[srcImageName];
		ImageAccess currentAccess = m_ImageAccess[srcImageName];

		if (m_ImageAccess[srcImageName].layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
			VulkanUtils::InsertImageBarrier(
				commandBuffer, srcImage.handle, VK_IMAGE_ASPECT_COLOR_BIT,
				currentAccess.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, currentAccess.stage_flags,
				VK_PIPELINE_STAGE_TRANSFER_BIT, currentAccess.access_flags, VK_ACCESS_TRANSFER_READ_BIT
			);
		}
		m_ImageAccess[srcImageName] = ImageAccess{
			.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.access_flags = VK_ACCESS_TRANSFER_READ_BIT,
			.stage_flags = VK_PIPELINE_STAGE_TRANSFER_BIT
		};

		VulkanUtils::InsertImageBarrier(commandBuffer, dstImage.handle, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, VK_ACCESS_TRANSFER_WRITE_BIT);

		VkImageCopy imageCopy{
			.srcSubresource = VkImageSubresourceLayers {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.layerCount = 1
			},
			.dstSubresource = VkImageSubresourceLayers {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.layerCount = 1
			},
			.extent = VkExtent3D {
				.width = srcImage.width,
				.height = srcImage.height,
				.depth = 1
			}
		};
		vkCmdCopyImage(
			commandBuffer,
			srcImage.handle,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			dstImage.handle,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&imageCopy
		);

		VulkanUtils::InsertImageBarrier(commandBuffer, dstImage.handle, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	bool RenderGraph::ContainsImage(std::string imageName) {
		return m_Images.find(imageName) != m_Images.end();
	}

	VkFormat RenderGraph::GetImageFormat(std::string imageName) {
		assert(m_Images.count(imageName));
		return m_Images[imageName].format;
	}

	std::vector<std::string> RenderGraph::GetColorAttachments() {
		std::vector<std::string> colorAttachmentNames;
		for (auto& [name, image] : m_Images) {
			if (!VulkanUtils::IsDepthFormat(image.format) && !name.ends_with("_MSAA")) {
				colorAttachmentNames.emplace_back(name);
			}
		}
		return colorAttachmentNames;
	}

	void RenderGraph::CreateGraphicsPass(RenderPassDescription& passDescription) {
		GraphicsPassDescription& graphicsPassDescription =
			std::get<GraphicsPassDescription>(passDescription.description);
		RenderPass renderPass{
			.name = passDescription.name,
			.pass = GraphicsPass {
				.callback = graphicsPassDescription.callback
			}
		};
		GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);

		uint32_t colorAttachmentCount = 0;
		uint32_t totalAttachmentCount = 0;
		for (TransientResource& output : passDescription.outputs) {
			if (output.type == TransientResourceType::Image &&
				output.image.type == TransientImageType::AttachmentImage) {
				if (!VulkanUtils::IsDepthFormat(output.image.format)) {
					++colorAttachmentCount;
				}
				++totalAttachmentCount;
			}
		}
		std::vector<VkAttachmentDescription> attachments(totalAttachmentCount);
		std::vector<VkAttachmentReference> colorAttachmentRefs(colorAttachmentCount);
		graphicsPass.attachments.resize(totalAttachmentCount);

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		std::vector<VkDescriptorImageInfo> descriptors;
		VkAttachmentReference depthAttachmentRef;
		VkSubpassDescription subpassDescription{
			.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		};
		
		auto addResourceToPass = [&](TransientResource& resource, bool inputResource) {
			if (resource.type == TransientResourceType::Image) {
				switch (resource.image.type) {
				case TransientImageType::AttachmentImage: {
					assert(!inputResource && "Attachment images must be outputs");
					bool isRenderOutput = !strcmp(resource.name, "RENDER_OUTPUT");

					VkImageLayout layout = VulkanUtils::GetImageLayoutFromResourceType(resource.image.type,
						resource.image.format);

					graphicsPass.attachments[resource.image.binding] = resource;
					attachments[resource.image.binding] = VkAttachmentDescription{
						.format = isRenderOutput ? m_Context.GetSwapChainImageFormat() : resource.image.format,
						.samples = resource.image.multisampled ? VK_SAMPLE_COUNT_8_BIT : VK_SAMPLE_COUNT_1_BIT,
						.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
						.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
						.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
						.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
						.finalLayout = isRenderOutput ? (
							resource.image.multisampled ?
							VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL :
							VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
						) : layout
					};

							if (VulkanUtils::IsDepthFormat(resource.image.format)) {
								assert(!subpassDescription.pDepthStencilAttachment);
							depthAttachmentRef = VkAttachmentReference{
									.attachment = resource.image.binding,
									.layout = layout
							};
							subpassDescription.pDepthStencilAttachment = &depthAttachmentRef;
						}
						else {
							colorAttachmentRefs[resource.image.binding] = VkAttachmentReference{
									.attachment = resource.image.binding,
									.layout = layout
							};
						}
				} break;
				case TransientImageType::SampledImage: {
					descriptors.emplace_back(
						VkDescriptorImageInfo{
							m_ResourceManager.GetDefaultSampler(), // Sampler
							m_Images[resource.name].view,
							VulkanUtils::GetImageLayoutFromResourceType(TransientImageType::SampledImage, resource.image.format) // Layout
						}
					);
					bindings.emplace_back(VkDescriptorSetLayoutBinding{
						resource.image.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
						1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr });
				} break;
				case TransientImageType::StorageImage: {
					descriptors.emplace_back(VkDescriptorImageInfo{
						nullptr, // Sampler
						m_Images[resource.name].view,
						VK_IMAGE_LAYOUT_GENERAL
						});
					bindings.emplace_back(VkDescriptorSetLayoutBinding{
						resource.image.binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr });
				} break;
				}
			}
			else if (resource.type == TransientResourceType::Buffer) {
				// TODO: Add Buffers
				assert(false);
			}
		};
		for (TransientResource& dependency : passDescription.dependencies) {
			addResourceToPass(dependency, true);
		}

		bool isMultisampledPass = false;
		for (TransientResource& output : passDescription.outputs) {
			addResourceToPass(output, false);
			if (output.type == TransientResourceType::Image) {
				if (output.image.multisampled) {
					isMultisampledPass = true;
				}
			}
		}

		VkAttachmentReference colorAttachmentResolveRef;
		if (isMultisampledPass) {
			attachments.emplace_back(VkAttachmentDescription{
				.format = m_Context.GetSwapChainImageFormat(),
				.samples = VK_SAMPLE_COUNT_1_BIT,
				.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
				.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
				.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
				.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
				});
			colorAttachmentResolveRef.attachment = static_cast<uint32_t>(attachments.size()) - 1;
			colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			subpassDescription.pResolveAttachments = &colorAttachmentResolveRef;
		}

		if (!bindings.empty()) {
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = static_cast<uint32_t>(bindings.size()),
				.pBindings = bindings.data()
			};
			VK_CHECK(vkCreateDescriptorSetLayout(m_Context.GetDevice(), &descriptorSetLayoutInfo,
				nullptr, &renderPass.descriptor_set_layout));
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = m_ResourceManager.GetTransientDescriptorPool(),
				.descriptorSetCount = 1,
				.pSetLayouts = &renderPass.descriptor_set_layout
			};
			VK_CHECK(vkAllocateDescriptorSets(m_Context.GetDevice(), &descriptorSetAllocInfo,
				&renderPass.descriptor_set));

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			for (uint32_t i = 0; i < descriptors.size(); ++i) {
				writeDescriptorSets.emplace_back(VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = renderPass.descriptor_set,
					.dstBinding = bindings[i].binding,
					.descriptorCount = 1,
					.descriptorType = descriptors[i].sampler ?
						VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
						VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &descriptors[i]
					});
			}

			vkUpdateDescriptorSets(m_Context.GetDevice(), static_cast<uint32_t>(writeDescriptorSets.size()),
				writeDescriptorSets.data(), 0, nullptr);
		}

		subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs.size());
		subpassDescription.pColorAttachments = colorAttachmentRefs.data();
		VkSubpassDependency subpassDependency{
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
		};

		VkRenderPassCreateInfo renderPassInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = static_cast<uint32_t>(attachments.size()),
			.pAttachments = attachments.data(),
			.subpassCount = 1,
			.pSubpasses = &subpassDescription,
			.dependencyCount = 1,
			.pDependencies = &subpassDependency
		};

		VK_CHECK(vkCreateRenderPass(m_Context.GetDevice(), &renderPassInfo, nullptr, &graphicsPass.handle));

		for (GraphicsPipelineDescription& pipelineDescription : graphicsPassDescription.pipeline_descriptions) {
			assert(m_GraphicsPipelines.find(pipelineDescription.name) == m_GraphicsPipelines.end());

			m_GraphicsPipelines[pipelineDescription.name] = VulkanUtils::CreateGraphicsPipeline(
				std::shared_ptr<VulkanContext>(&m_Context, [](VulkanContext*) {}), 
				m_ResourceManager, renderPass, pipelineDescription);
		}

		m_Passes[renderPass.name] = renderPass;
	}

	void RenderGraph::CreateRaytracingPass(RenderPassDescription& passDescription) {
		RaytracingPassDescription& raytracingPassDescription =
			std::get<RaytracingPassDescription>(passDescription.description);
		RenderPass renderPass{
			.name = passDescription.name,
			.pass = RaytracingPass {
				.callback = raytracingPassDescription.callback
			}
		};

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		std::vector<VkDescriptorImageInfo> descriptors;
		auto addResourceToPass = [&](TransientResource& resource) {
			if (resource.type == TransientResourceType::Image) {
				assert(resource.image.type != TransientImageType::AttachmentImage &&
					"Attachment images are not allowed in raytracing passes");
				switch (resource.image.type) {
				case TransientImageType::SampledImage: {
					descriptors.emplace_back(
						VkDescriptorImageInfo{
							m_ResourceManager.GetDefaultSampler(),
							m_Images[resource.name].view,
							VulkanUtils::GetImageLayoutFromResourceType(TransientImageType::SampledImage, resource.image.format)
						}
					);
					bindings.emplace_back(VkDescriptorSetLayoutBinding{
						resource.image.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
						1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr });
				} break;
				case TransientImageType::StorageImage: {
					descriptors.emplace_back(VkDescriptorImageInfo{
						nullptr,
						m_Images[resource.name].view,
						VK_IMAGE_LAYOUT_GENERAL
						});
					bindings.emplace_back(VkDescriptorSetLayoutBinding{
						resource.image.binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr });
				} break;
				}
			}
			else if (resource.type == TransientResourceType::Buffer) {
				// TODO: Add Buffers
				assert(false);
			}
		};

		for (TransientResource& dependency : passDescription.dependencies) {
			addResourceToPass(dependency);
		}
		for (TransientResource& output : passDescription.outputs) {
			addResourceToPass(output);
		}

		if (!passDescription.dependencies.empty() || !passDescription.outputs.empty()) {
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = static_cast<uint32_t>(bindings.size()),
				.pBindings = bindings.data()
			};
			VK_CHECK(vkCreateDescriptorSetLayout(m_Context.GetDevice(), &descriptorSetLayoutInfo,
				nullptr, &renderPass.descriptor_set_layout));
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = m_ResourceManager.GetTransientDescriptorPool(),
				.descriptorSetCount = 1,
				.pSetLayouts = &renderPass.descriptor_set_layout
			};
			VK_CHECK(vkAllocateDescriptorSets(m_Context.GetDevice(), &descriptorSetAllocInfo,
				&renderPass.descriptor_set));

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			for (uint32_t i = 0; i < descriptors.size(); ++i) {
				writeDescriptorSets.emplace_back(VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = renderPass.descriptor_set,
					.dstBinding = i,
					.descriptorCount = 1,
					.descriptorType = descriptors[i].sampler ?
						VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
						VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &descriptors[i]
					});
			}

			vkUpdateDescriptorSets(m_Context.GetDevice(), static_cast<uint32_t>(writeDescriptorSets.size()),
				writeDescriptorSets.data(), 0, nullptr);
		}

		assert(m_RaytracingPipelines.find(raytracingPassDescription.pipeline_description.name) == m_RaytracingPipelines.end());
		
		m_RaytracingPipelines[raytracingPassDescription.pipeline_description.name] = VulkanUtils::CreateRaytracingPipeline(
			std::shared_ptr<VulkanContext>(&m_Context, [](VulkanContext*) {}), 
			m_ResourceManager, renderPass, raytracingPassDescription.pipeline_description,
			m_Context.GetRayTracingProperties());

		m_Passes[renderPass.name] = renderPass;
	}

	void RenderGraph::CreateComputePass(RenderPassDescription& passDescription) {
		ComputePassDescription& computePassDescription =
			std::get<ComputePassDescription>(passDescription.description);
		RenderPass renderPass{
			.name = passDescription.name,
			.pass = ComputePass {
				.callback = computePassDescription.callback
			}
		};

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		std::vector<VkDescriptorImageInfo> descriptors;
		auto addResourceToPass = [&](TransientResource& resource) {
			if (resource.type == TransientResourceType::Image) {
				assert(resource.image.type != TransientImageType::AttachmentImage &&
					"Attachment images are not allowed in compute passes");
				switch (resource.image.type) {
				case TransientImageType::SampledImage: {
					descriptors.emplace_back(
						VkDescriptorImageInfo{
							m_ResourceManager.GetDefaultSampler(),
							m_Images[resource.name].view,
							VulkanUtils::GetImageLayoutFromResourceType(TransientImageType::SampledImage, resource.image.format)
						}
					);
					bindings.emplace_back(VkDescriptorSetLayoutBinding{
						resource.image.binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
						1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
				} break;
				case TransientImageType::StorageImage: {
					descriptors.emplace_back(VkDescriptorImageInfo{
						nullptr,
						m_Images[resource.name].view,
						VK_IMAGE_LAYOUT_GENERAL
						});
					bindings.emplace_back(VkDescriptorSetLayoutBinding{
						resource.image.binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
				} break;
				}
			}
			else if (resource.type == TransientResourceType::Buffer) {
				// TODO: Add Buffers
				assert(false);
			}
		};

		for (TransientResource& dependency : passDescription.dependencies) {
			addResourceToPass(dependency);
		}
		for (TransientResource& output : passDescription.outputs) {
			addResourceToPass(output);
		}

		if (!passDescription.dependencies.empty() || !passDescription.outputs.empty()) {
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = static_cast<uint32_t>(bindings.size()),
				.pBindings = bindings.data()
			};
			VK_CHECK(vkCreateDescriptorSetLayout(m_Context.GetDevice(), &descriptorSetLayoutInfo,
				nullptr, &renderPass.descriptor_set_layout));
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
				.descriptorPool = m_ResourceManager.GetTransientDescriptorPool(),
				.descriptorSetCount = 1,
				.pSetLayouts = &renderPass.descriptor_set_layout
			};
			VK_CHECK(vkAllocateDescriptorSets(m_Context.GetDevice(), &descriptorSetAllocInfo,
				&renderPass.descriptor_set));

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			for (uint32_t i = 0; i < descriptors.size(); ++i) {
				writeDescriptorSets.emplace_back(VkWriteDescriptorSet{
					.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
					.dstSet = renderPass.descriptor_set,
					.dstBinding = i,
					.descriptorCount = 1,
					.descriptorType = descriptors[i].sampler ?
						VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
						VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					.pImageInfo = &descriptors[i]
					});
			}

			vkUpdateDescriptorSets(m_Context.GetDevice(), static_cast<uint32_t>(writeDescriptorSets.size()),
				writeDescriptorSets.data(), 0, nullptr);
		}


		// Create compute pipelines of all associated kernels
		for (ComputeKernel& kernel : computePassDescription.pipeline_description.kernels) {
			assert(m_ComputePipelines.find(kernel.shader) == m_ComputePipelines.end() && "Compute shader already loaded!");
			m_ComputePipelines[kernel.shader] = VulkanUtils::CreateComputePipeline(
				std::shared_ptr<VulkanContext>(&m_Context, [](VulkanContext*) {}),
				m_ResourceManager, renderPass, computePassDescription.pipeline_description.push_constant_description,
				kernel);
		}

		m_Passes[renderPass.name] = renderPass;
	}

	void RenderGraph::FindExecutionOrder() {
		assert(m_Writers["RENDER_OUTPUT"].size() == 1);

		// Traverse from the final pass back and insert all dependent passes 
		m_ExecutionOrder = { m_Writers["RENDER_OUTPUT"][0] };
		std::deque<std::string> stack{ m_Writers["RENDER_OUTPUT"][0] };
		while (!stack.empty()) {
			std::string& passName = stack.front();
			stack.pop_front();
			
			// Fix: check existence
			if (m_PassDescriptions.find(passName) != m_PassDescriptions.end()) {
				RenderPassDescription& pass = m_PassDescriptions[passName];
				for (TransientResource& dependency : pass.dependencies) {
					for (std::string& writer : m_Writers[dependency.name]) {
						m_ExecutionOrder.push_back(writer);
						stack.push_back(writer);
					}
				}
			}
		}

		// Reverse the list
		std::reverse(m_ExecutionOrder.begin(), m_ExecutionOrder.end());

		// Prune duplicates
		std::vector<std::string> found;

		std::vector<std::string>::iterator it = m_ExecutionOrder.begin();
		while (it != m_ExecutionOrder.end()) {
			if (std::find(found.begin(), found.end(), *it) == found.end()) {
				found.emplace_back(*it);
				++it;
			}
			else {
				it = m_ExecutionOrder.erase(it);
			}
		}
	}

	void RenderGraph::InsertBarriers(VkCommandBuffer commandBuffer, RenderPass& renderPass) {
		RenderPassDescription& passDescription = m_PassDescriptions[renderPass.name];
		bool isGraphicsPass = std::holds_alternative<GraphicsPass>(renderPass.pass);
		VkPipelineStageFlags dstStage = isGraphicsPass ?
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT :
			VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;

		bool transitionsStarted = false;

		auto insertBarrierIfNeeded = [&](TransientResource& resource,
			VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
			ImageAccess currentAccess = m_ImageAccess[resource.name];
			VkImageLayout dstLayout = VulkanUtils::GetImageLayoutFromResourceType(resource.image.type,
				resource.image.format);

			if (strcmp(resource.name, "RENDER_OUTPUT") && currentAccess.layout != dstLayout) {
				VkImageAspectFlags aspectFlags = VulkanUtils::IsDepthFormat(resource.image.format) ?
					VK_IMAGE_ASPECT_DEPTH_BIT :
					VK_IMAGE_ASPECT_COLOR_BIT;

				if (!transitionsStarted) {
					VkDebugUtilsLabelEXT passLabel{
						.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
						.pLabelName = "Image Transitions"
					};
				vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &passLabel);
				transitionsStarted = true;
				}

				VulkanUtils::InsertImageBarrier(commandBuffer, m_Images[resource.name].handle,
					aspectFlags, currentAccess.layout, dstLayout, currentAccess.stage_flags,
					dstStage, currentAccess.access_flags, dstAccess);

				m_ImageAccess[resource.name] = ImageAccess{
					.layout = dstLayout,
					.access_flags = dstAccess,
					.stage_flags = dstStage
				};
			}
		};

		for (TransientResource& dependency : passDescription.dependencies) {
			if (dependency.type == TransientResourceType::Image) {
				insertBarrierIfNeeded(dependency, dstStage, VK_ACCESS_SHADER_READ_BIT);
			}
			else if (dependency.type == TransientResourceType::Buffer) {
				// TODO: Buffer
				assert(false);
			}
		}
		for (TransientResource& output : passDescription.outputs) {
			if (output.type == TransientResourceType::Image) {
				if (output.image.type == TransientImageType::AttachmentImage) {
					// Implicit barrier through render pass
					m_ImageAccess[output.name] = ImageAccess{
						.layout = VulkanUtils::GetImageLayoutFromResourceType(output.image.type,
							output.image.format),
						.access_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						.stage_flags = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
					};
				}
				else {
					insertBarrierIfNeeded(output, dstStage, VK_ACCESS_SHADER_WRITE_BIT);
				}
			}
			else if (output.type == TransientResourceType::Buffer) {
				// TODO: Buffer
				assert(false);
			}
		}

		if (transitionsStarted) {
			vkCmdEndDebugUtilsLabelEXT(commandBuffer);
		}
	}

	void RenderGraph::ExecuteGraphicsPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx,
		uint32_t imageIdx, RenderPass& renderPass) {
		GraphicsPass& graphicsPass = std::get<GraphicsPass>(renderPass.pass);

		VkFramebuffer& framebuffer = graphicsPass.framebuffers[resourceIdx];

		// Delete previous framebuffer
		if (framebuffer != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(m_Context.GetDevice(), framebuffer, nullptr);
			framebuffer = VK_NULL_HANDLE;
		}

		bool isMultisampledPass = false;
		std::vector<VkImageView> imageViews;
		std::vector<VkClearValue> clearValues;
		for (TransientResource& attachment : graphicsPass.attachments) {
			bool isRenderOutput = !strcmp(attachment.name, "RENDER_OUTPUT");
			if (isRenderOutput) {
				if (attachment.image.multisampled) {
					imageViews.emplace_back(m_Images[std::string(renderPass.name) + "_MSAA"].view);
					isMultisampledPass = true;
				}
				else {
					imageViews.emplace_back(m_Context.GetSwapChainImageViews()[imageIdx]);
				}
			}
			else {
				imageViews.emplace_back(m_Images[attachment.name].view);
			}
			clearValues.emplace_back(attachment.image.clear_value);
		}
		if (isMultisampledPass) {
			imageViews.emplace_back(m_Context.GetSwapChainImageViews()[imageIdx]);
		}

		uint32_t passWidth = graphicsPass.attachments[0].image.width;
		uint32_t passHeight = graphicsPass.attachments[0].image.height;
		VkFramebufferCreateInfo framebufferInfo{
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = graphicsPass.handle,
			.attachmentCount = static_cast<uint32_t>(imageViews.size()),
			.pAttachments = imageViews.data(),
			.width = passWidth == 0 ? m_Context.GetSwapChainExtent().width : passWidth,
			.height = passHeight == 0 ? m_Context.GetSwapChainExtent().height : passHeight,
			.layers = 1
		};

		VK_CHECK(vkCreateFramebuffer(m_Context.GetDevice(), &framebufferInfo, nullptr, &framebuffer));

		VkRenderPassBeginInfo renderPassBeginInfo{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = graphicsPass.handle,
			.framebuffer = framebuffer,
			.renderArea = VkRect2D {
				.offset = VkOffset2D {.x = 0, .y = 0 },
				.extent = (passWidth == 0 || passHeight == 0) ?
					m_Context.GetSwapChainExtent() :
					VkExtent2D {
						.width = passWidth,
						.height = passHeight
					}
			},
			.clearValueCount = static_cast<uint32_t>(clearValues.size()),
			.pClearValues = clearValues.data()
		};

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

					graphicsPass.callback(
						[&](std::string pipelineName, GraphicsExecutionCallback executePipeline) {
							GraphicsPipeline& pipeline = m_GraphicsPipelines[pipelineName];
		
							vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
							
							// Use resourceIdx to get correct per-frame set for all slots if they use the same layout
							VkDescriptorSet currentSet = m_ResourceManager.GetGlobalDescriptorSet(resourceIdx);
							vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
								pipeline.layout, 0, 1, &currentSet, 0, nullptr);
							vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
								pipeline.layout, 1, 1, &currentSet, 0, nullptr);
							vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
								pipeline.layout, 2, 1, &currentSet, 0, nullptr);
		
							if (renderPass.descriptor_set != VK_NULL_HANDLE) {
								vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout,
									3, 1, &renderPass.descriptor_set, 0, nullptr);
							}
							GraphicsExecutionContext executionContext(commandBuffer, m_ResourceManager, pipeline);
							executePipeline(executionContext);
						}
					);
		vkCmdEndRenderPass(commandBuffer);
	}

	void RenderGraph::ExecuteRaytracingPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass) {
		RaytracingPass& raytracingPass = std::get<RaytracingPass>(renderPass.pass);

		raytracingPass.callback(
			[&](std::string pipelineName, RaytracingExecutionCallback executePipeline) {
				RaytracingPipeline& pipeline = m_RaytracingPipelines[pipelineName];

				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.handle);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
					pipeline.layout, 0, 1, &m_ResourceManager.GetGlobalDescriptorSet0(), 0, nullptr);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
					pipeline.layout, 1, 1, &m_ResourceManager.GetGlobalDescriptorSet1(), 0, nullptr);
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
					pipeline.layout, 2, 1, &m_ResourceManager.GetPerFrameDescriptorSets()[resourceIdx], 0, nullptr);
				if (renderPass.descriptor_set != VK_NULL_HANDLE) {
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.layout, 
						3, 1, &renderPass.descriptor_set, 0, nullptr);
				}

				RaytracingExecutionContext executionContext(commandBuffer, m_ResourceManager, pipeline);
				executePipeline(executionContext);
			}
		);
	}

	void RenderGraph::ExecuteComputePass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass) {
		ComputePass& computePass = std::get<ComputePass>(renderPass.pass);

		ComputeExecutionContext executionContext(commandBuffer, renderPass, *this, m_ResourceManager, resourceIdx);
		computePass.callback(executionContext);
	}

	void RenderGraph::ActualizeResource(TransientResource& resource, const char* renderPassName) {
		// Use m_Context.GetMSAASamples() directly instead of VulkanUtils::GetMaxMultisampleCount
		VkSampleCountFlagBits maxMultisampleCount = m_Context.GetMSAASamples();

		if (!strcmp(resource.name, "RENDER_OUTPUT")) {
			assert(resource.type == TransientResourceType::Image);
			// If the render output is multisampled, create MSAA image to resolve from
			if (resource.image.multisampled) {
				std::string msaaImageName = std::string(renderPassName) + "_MSAA";
				m_Images[msaaImageName] = m_ResourceManager.CreateGraphImage(
					m_Context.GetSwapChainExtent().width,
					m_Context.GetSwapChainExtent().height,
					m_Context.GetSwapChainImageFormat(),
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
					VK_IMAGE_LAYOUT_UNDEFINED,
					maxMultisampleCount
				);
				m_ImageAccess[msaaImageName] = ImageAccess{
					.layout = VK_IMAGE_LAYOUT_UNDEFINED,
					.access_flags = 0,
					.stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
				};
				m_ResourceManager.TagImage(m_Images[msaaImageName], msaaImageName.c_str());
			}

			return;
		}

		if (m_Images.find(resource.name) == m_Images.end()) {
			VkImageUsageFlags usage = VulkanUtils::IsDepthFormat(resource.image.format) ?
				VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT :
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
				VK_IMAGE_USAGE_TRANSFER_DST_BIT;

			// Swapchain-sized image
			if (resource.image.width == 0 && resource.image.height == 0) {
				m_Images[resource.name] = m_ResourceManager.CreateGraphImage(
					m_Context.GetSwapChainExtent().width,
					m_Context.GetSwapChainExtent().height, 
					resource.image.format, 
					usage, 
					VK_IMAGE_LAYOUT_GENERAL,
					resource.image.multisampled ? maxMultisampleCount : VK_SAMPLE_COUNT_1_BIT);
			}
			else {
				m_Images[resource.name] = m_ResourceManager.CreateGraphImage(
					resource.image.width,
					resource.image.height, 
					resource.image.format, 
					usage, 
					VK_IMAGE_LAYOUT_GENERAL,
					resource.image.multisampled ? maxMultisampleCount : VK_SAMPLE_COUNT_1_BIT);
			}
			m_ImageAccess[resource.name] = ImageAccess{
				.layout = VK_IMAGE_LAYOUT_GENERAL,
				.access_flags = 0,
				.stage_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
			};
			m_ResourceManager.TagImage(m_Images[resource.name], resource.name);
		}
	}

	bool RenderGraph::SanityCheck() {
		std::unordered_map<std::string, std::vector<TransientResource>> participatingResources;
		for (std::string& passName : m_ExecutionOrder) {
			RenderPassDescription& pass = m_PassDescriptions[passName];
			for (TransientResource& dependency : pass.dependencies) {
				participatingResources[dependency.name].emplace_back(dependency);
			}
			for (TransientResource& output : pass.outputs) {
				participatingResources[output.name].emplace_back(output);
			}
		}

		for (auto& [name, resources] : participatingResources) {
			if (!strcmp(name.c_str(), "RENDER_OUTPUT")) {
				continue;
			}

			if (resources.empty()) {
				return false;
			}

			if (resources.front().type == TransientResourceType::Image) {
				uint32_t width = resources.front().image.width;
				uint32_t height = resources.front().image.height;
				VkFormat format = resources.front().image.format;

				for (TransientResource& resource : resources) {
					if (resource.image.width != width ||
						resource.image.height != height ||
						resource.image.format != format) {
						return false;
					}
				}

			} else if (resources.front().type == TransientResourceType::Buffer) {
				// TODO: Buffers
				assert(false);
			}
		}
		return true;
	}
}
