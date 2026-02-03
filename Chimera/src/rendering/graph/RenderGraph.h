#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanCommon.h"

namespace Chimera {

	class VulkanContext;
	class ResourceManager;
	class GraphicsExecutionContext;
	class RaytracingExecutionContext;
	class ComputeExecutionContext;
	class Image;

	class RenderGraph {
	public:
		RenderGraph(VulkanContext& context, ResourceManager& resourceManager);
		~RenderGraph();

		void DestroyResources();

		void AddGraphicsPass(const char* renderPassName, std::vector<TransientResource> dependencies,
			std::vector<TransientResource> outputs, std::vector<GraphicsPipelineDescription> pipelines,
			GraphicsPassCallback callback);
		void AddRaytracingPass(const char* renderPassName, std::vector<TransientResource> dependencies,
			std::vector<TransientResource> outputs, RaytracingPipelineDescription pipeline,
			RaytracingPassCallback callback);
		void AddComputePass(const char* renderPassName, std::vector<TransientResource> dependencies,
			std::vector<TransientResource> outputs, ComputePipelineDescription pipeline,
			ComputePassCallback callback);

		void Build();
		void Execute(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx);
		void GatherPerformanceStatistics();
		void DrawPerformanceStatistics();
		void CopyImage(VkCommandBuffer commandBuffer, std::string srcImageName, Image& dstImage);
		void CopyImage(VkCommandBuffer commandBuffer, std::string srcImageName, GraphImage& dstImage);
		bool ContainsImage(std::string imageName);
		VkFormat GetImageFormat(std::string imageName);
		std::vector<std::string> GetColorAttachments();

		void RegisterExternalResource(const std::string& name, const ImageDescription& description);
		void SetExternalResource(const std::string& name, VkImage handle, VkImageView view, VkImageLayout currentLayout, VkAccessFlags currentAccess, VkPipelineStageFlags currentStage);

	private:
		void CreateGraphicsPass(RenderPassDescription& passDescription);
		void CreateRaytracingPass(RenderPassDescription& passDescription);
		void CreateComputePass(RenderPassDescription& passDescription);

		void FindExecutionOrder();
		void InsertBarriers(VkCommandBuffer commandBuffer, RenderPass& renderPass);
		void ExecuteGraphicsPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, uint32_t imageIdx, RenderPass& renderPass);
		void ExecuteRaytracingPass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass);
		void ExecuteComputePass(VkCommandBuffer commandBuffer, uint32_t resourceIdx, RenderPass& renderPass);
		void ActualizeResource(TransientResource& resource, const char* renderPassName);
		bool SanityCheck();

		VulkanContext& m_Context;
		ResourceManager& m_ResourceManager;
		VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;

		std::vector<std::string> m_ExecutionOrder;
		std::unordered_map<std::string, std::vector<std::string>> m_Readers;
		std::unordered_map<std::string, std::vector<std::string>> m_Writers;
		std::unordered_map<std::string, RenderPassDescription> m_PassDescriptions;
		std::unordered_map<std::string, RenderPass> m_Passes;
		std::unordered_map<std::string, GraphicsPipeline> m_GraphicsPipelines;
		std::unordered_map<std::string, RaytracingPipeline> m_RaytracingPipelines;
		std::unordered_map<std::string, ComputePipeline> m_ComputePipelines;
		std::unordered_map<std::string, GraphImage> m_Images; // Note: Changed Image to GraphImage based on ResourceManager
		std::unordered_map<std::string, ImageAccess> m_ImageAccess;
		std::unordered_map<std::string, double> m_PassTimestamps;

		friend class RenderPath;
		friend class ComputeExecutionContext;
		friend class GraphicsExecutionContext;
		friend class RaytracingExecutionContext;
	};

}
