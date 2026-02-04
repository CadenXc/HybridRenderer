#include "pch.h"
#include "Utils/VulkanDescriptorUtils.h"

namespace Chimera::VulkanUtils {

	VkDescriptorImageInfo DescriptorImageInfo(VkImageView view, VkImageLayout layout, VkSampler sampler)
	{
		VkDescriptorImageInfo info{};
		info.imageView = view;
		info.imageLayout = layout;
		info.sampler = sampler;
		return info;
	}

	VkDescriptorSetLayoutBinding DescriptorSetLayoutBinding(
		uint32_t binding, 
		VkDescriptorType descriptorType, 
		VkShaderStageFlags stageFlags, 
		uint32_t descriptorCount
	) {
		VkDescriptorSetLayoutBinding layoutBinding{};
		layoutBinding.binding = binding;
		layoutBinding.descriptorType = descriptorType;
		layoutBinding.descriptorCount = descriptorCount;
		layoutBinding.stageFlags = stageFlags;
		layoutBinding.pImmutableSamplers = nullptr;
		return layoutBinding;
	}
}

