#pragma once

#include "pch.h"
#include "gfx/vulkan/VulkanCommon.h"
#include "gfx/resources/Buffer.h"
#include "gfx/resources/Image.h"

namespace Chimera {

	struct UniformBufferObject
	{
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 proj;
		glm::mat4 prevView;
		glm::mat4 prevProj;
		glm::vec4 lightPos;
		int frameCount;
	};

	class ResourceManager
	{
	public:
		ResourceManager(std::shared_ptr<class VulkanContext> context);
		~ResourceManager();

		void InitGlobalResources();
		void UpdateGlobalResources(uint32_t currentFrame, const UniformBufferObject& ubo);

		// Descriptor Layouts
		VkDescriptorSetLayout GetGlobalDescriptorSetLayout() const { return m_DescriptorSetLayout; }
		VkDescriptorSetLayout GetGlobalDescriptorSetLayout0() const { return m_DescriptorSetLayout; }
		VkDescriptorSetLayout GetGlobalDescriptorSetLayout1() const { return m_DescriptorSetLayout; }
		VkDescriptorSetLayout GetPerFrameDescriptorSetLayout() const { return m_DescriptorSetLayout; }

		const std::vector<VkDescriptorSet>& GetPerFrameDescriptorSets() const { return m_GlobalDescriptorSets; }

		VkDescriptorSet& GetGlobalDescriptorSet(uint32_t frame) { return m_GlobalDescriptorSets[frame]; }
		VkDescriptorSet& GetGlobalDescriptorSet0() { return m_GlobalDescriptorSets[0]; }
		VkDescriptorSet& GetGlobalDescriptorSet1() { return m_GlobalDescriptorSets[0]; }
		
		VkDescriptorPool GetTransientDescriptorPool() { return m_TransientDescriptorPool; }

		// Samplers
		VkSampler GetDefaultSampler() const { return m_TextureSampler; }

		// Graph Resource Creation
		GraphImage CreateGraphImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageLayout initialLayout, VkSampleCountFlagBits samples);
		void DestroyGraphImage(GraphImage& image);
		void TagImage(GraphImage& image, const char* name);

		// Textures
		std::unique_ptr<Image> LoadTexture(const std::string& path);

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
		VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
		
		std::vector<VkDescriptorSet> m_GlobalDescriptorSets;
		std::vector<std::unique_ptr<Buffer>> m_UniformBuffers;
		
		std::unique_ptr<Image> m_GlobalTexture;
		VkSampler m_TextureSampler = VK_NULL_HANDLE;
	};

}