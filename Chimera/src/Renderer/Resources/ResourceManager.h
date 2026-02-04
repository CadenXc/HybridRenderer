#pragma once

#include "pch.h"
#include "Renderer/Backend/VulkanCommon.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Resources/Image.h"

namespace Chimera {

	struct UniformBufferObject
	{
		glm::mat4 view;
		glm::mat4 proj;
		glm::mat4 prevView;
		glm::mat4 prevProj;
		glm::vec4 cameraPos;
		glm::vec4 lightPos;
		float time;
		int frameCount;
		float padding[2];
	};

	class ResourceManager
	{
	public:
		ResourceManager(std::shared_ptr<class VulkanContext> context);
		~ResourceManager();

		void InitGlobalResources();
		void UpdateGlobalResources(uint32_t currentFrame, const UniformBufferObject& ubo);

		VkDescriptorSetLayout GetGlobalDescriptorSetLayout() const { return m_GlobalDescriptorSetLayout; }
		VkDescriptorSet GetGlobalDescriptorSet(uint32_t frameIndex) const { return m_GlobalDescriptorSets[frameIndex]; }
		
		VkDescriptorPool GetTransientDescriptorPool() { return m_TransientDescriptorPool; }

		// Samplers
		VkSampler GetDefaultSampler() const { return m_TextureSampler; }

		// Graph Resource Creation
		GraphImage CreateGraphImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageLayout initialLayout, VkSampleCountFlagBits samples);
		
		// For Aliasing
		VkMemoryRequirements GetImageMemoryRequirements(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples);
		GraphImage CreateImageAliased(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkSampleCountFlagBits samples, VkDeviceMemory sharedMemory, VkDeviceSize offset);

		void DestroyGraphImage(GraphImage& image);
		void TagImage(GraphImage& image, const char* name);

		// Textures
		std::unique_ptr<Image> LoadTexture(const std::string& path);
		uint32_t AddTexture(std::unique_ptr<Image> texture);

	private:
		void CreateDescriptorSetLayout();
		void CreateDescriptorPool();
		void CreateTransientDescriptorPool();
		void CreateDescriptorSets();
		void CreateUniformBuffers();
		void CreateTextureSampler();

	private:
		std::shared_ptr<class VulkanContext> m_Context;
		
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
		VkDescriptorPool m_TransientDescriptorPool = VK_NULL_HANDLE;
		
		VkDescriptorSetLayout m_GlobalDescriptorSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GlobalDescriptorSets; 
		
		std::vector<std::unique_ptr<Buffer>> m_UniformBuffers;
		
		std::vector<std::unique_ptr<Image>> m_Textures;
		VkSampler m_TextureSampler = VK_NULL_HANDLE;
	};

}
