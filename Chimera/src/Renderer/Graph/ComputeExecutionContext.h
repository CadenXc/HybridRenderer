#pragma once

#include "pch.h"
#include "RenderGraph.h"

namespace Chimera {

	class RenderGraph;
	class ResourceManager;
	class ComputeExecutionContext {
	public:
		ComputeExecutionContext(VkCommandBuffer commandBuffer, RenderPass& renderPass,
			RenderGraph& renderGraph, ResourceManager& resourceManager, uint32_t resourceIdx) :
			m_CommandBuffer(commandBuffer),
			m_RenderPass(renderPass),
			m_RenderGraph(renderGraph),
			m_ResourceManager(resourceManager),
			m_ResourceIdx(resourceIdx)
		{}

		glm::uvec2 GetDisplaySize();
		void Dispatch(const char* entry, uint32_t xGroups, uint32_t yGroups, uint32_t zGroups);

		void BindGlobalSet(uint32_t slot, uint32_t frameIndex, const ComputePipeline& pipeline);
		void BindPassSet(uint32_t slot, VkDescriptorSet set, const ComputePipeline& pipeline);

		template<typename T>
		void Dispatch(const char* entry, uint32_t xGroups, uint32_t yGroups, uint32_t zGroups, T& pushConstants) {
			ComputePipeline* pipeline = m_RenderGraph.m_ComputePipelines[entry];
			assert(sizeof(T) == pipeline->push_constant_description.size);
			vkCmdPushConstants(m_CommandBuffer, pipeline->layout, pipeline->push_constant_description.shader_stage,
				0, pipeline->push_constant_description.size, &pushConstants);
			Dispatch(entry, xGroups, yGroups, zGroups);
		}

		// Blit methods commented out due to missing storage_images in ResourceManager
		/*
		void BlitImageStorageToTransient(int src, const char *dst);
		void BlitImageTransientToStorage(const char *src, int dst);
		void BlitImageStorageToStorage(int src, int dst);
		*/

	private:
		// void BlitImage(Image src, Image dst);

		VkCommandBuffer m_CommandBuffer;
		RenderPass& m_RenderPass;
		RenderGraph& m_RenderGraph;
		ResourceManager& m_ResourceManager;
		uint32_t m_ResourceIdx;
	};

}
