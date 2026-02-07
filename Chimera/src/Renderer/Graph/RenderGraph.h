#pragma once

#include "pch.h"
#include "Renderer/Backend/VulkanCommon.h"
#include <deque>

namespace Chimera {

	class VulkanContext;
	class ResourceManager;
	class GraphicsExecutionContext;
	class RaytracingExecutionContext;
	class ComputeExecutionContext;
	class Image;
	class PipelineManager;

	class RenderGraph {
	public:
		RenderGraph(VulkanContext& context, ResourceManager& resourceManager, PipelineManager& pipelineManager, uint32_t width = 0, uint32_t height = 0);
		~RenderGraph();

		void DestroyResources(bool forceAll = false);

		void AddGraphicsPass(const GraphicsPassSpecification& spec);
		void AddRaytracingPass(const RaytracingPassSpecification& spec);
		void AddComputePass(const ComputePassSpecification& spec);

		void AddGraphicsPass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
			std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines,
			GraphicsPassCallback callback, const std::string& shaderLayoutName = "");
		void AddRaytracingPass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
			std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline,
			RaytracingPassCallback callback, const std::string& shaderLayoutName = "");
		void AddComputePass(const std::string& renderPassName, std::vector<TransientResource> dependencies,
			std::vector<TransientResource> outputs, ComputePipelineDescription pipeline, ComputePassCallback callback, const std::string& shaderLayoutName = "");
		void AddBlitPass(const std::string& renderPassName, const std::string& srcImageName, const std::string& dstImageName, VkFormat srcFormat = VK_FORMAT_UNDEFINED, VkFormat dstFormat = VK_FORMAT_UNDEFINED);
		
		void Build();
		void Execute(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx, std::function<void(VkCommandBuffer)> uiDrawCallback = nullptr);
		void GatherPerformanceStatistics();
		void DrawPerformanceStatistics();
		void CopyImage(VkCommandBuffer commandBuffer, std::string srcImageName, Image& dstImage);
		void CopyImage(VkCommandBuffer commandBuffer, std::string srcImageName, GraphImage& dstImage);
		void BlitImage(VkCommandBuffer commandBuffer, std::string srcImageName, std::string dstImageName, uint32_t imageIdx);
		bool ContainsImage(std::string imageName);
		
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		GraphImage& GetImage(const std::string& name) { return m_Images[name]; }
		ImageAccess& GetImageAccess(const std::string& name) { return m_ImageAccess[name]; }

		VkFormat GetImageFormat(std::string imageName);
		std::vector<std::string> GetColorAttachments();

		void RegisterExternalResource(const std::string& name, const ImageDescription& description);
		void SetExternalResource(const std::string& name, VkImage handle, VkImageView view, VkImageLayout currentLayout, VkAccessFlags currentAccess, VkPipelineStageFlags currentStage);
		void SetExternalResource(const std::string& name, VkImage handle, VkImageView view, VkImageLayout currentLayout, const ImageDescription& description);

	private:
		void CreateGraphicsPass(RenderPassDescription& passDescription);
		void CreateRaytracingPass(RenderPassDescription& passDescription);
		void CreateComputePass(RenderPassDescription& passDescription);

		void CreateFramebuffers(RenderPass& renderPass);

		// Helper methods for refactoring
		void CreatePassDescriptorSet(RenderPass& renderPass, RenderPassDescription& passDescription, VkShaderStageFlags stageFlags);
		void ParseGraphicsAttachments(RenderPassDescription& passDescription, GraphicsPass& graphicsPass, 
			std::vector<VkAttachmentDescription>& attachments, std::vector<VkAttachmentReference>& colorRefs, 
			VkAttachmentReference& depthRef, bool& isMultisampled);

		void FindExecutionOrder();
		void InsertBarriers(VkCommandBuffer commandBuffer, RenderPass& renderPass, uint32_t imageIdx);
		void ExecuteGraphicsPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx, RenderPass& renderPass);
		void ExecuteRaytracingPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass);
		void ExecuteComputePass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass);
		bool SanityCheck();

		struct ResourceLifetime
		{
			uint32_t first_pass = 0xFFFFFFFF;
			uint32_t last_pass = 0;
		};

		VulkanContext& m_Context;
		ResourceManager& m_ResourceManager;
		PipelineManager& m_PipelineManager;
		VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;
		bool m_QueryPoolReset = false;

		std::vector<std::string> m_ExecutionOrder;
		std::unordered_map<std::string, std::vector<std::string>> m_Readers;
		std::unordered_map<std::string, std::vector<std::string>> m_Writers;
		std::unordered_map<std::string, RenderPassDescription> m_PassDescriptions;
		std::unordered_map<std::string, RenderPass> m_Passes;
		std::unordered_map<std::string, GraphicsPipeline*> m_GraphicsPipelines;
		std::unordered_map<std::string, RaytracingPipeline*> m_RaytracingPipelines;
		std::unordered_map<std::string, ComputePipeline*> m_ComputePipelines;
		std::unordered_map<std::string, GraphImage> m_Images; 
		std::unordered_map<std::string, ImageAccess> m_ImageAccess;
		std::unordered_map<std::string, double> m_PassTimestamps;
        std::deque<std::vector<VkDescriptorImageInfo>> m_SamplerArrays;
		
		std::unordered_map<std::string, ResourceLifetime> m_ResourceLifetimes;
		VkDeviceMemory m_SharedMemory = VK_NULL_HANDLE;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		friend class RenderPath;
		friend class ComputeExecutionContext;
		friend class GraphicsExecutionContext;
		friend class RaytracingExecutionContext;
	};

}
