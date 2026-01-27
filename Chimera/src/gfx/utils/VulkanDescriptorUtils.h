#pragma once
#include "pch.h"

namespace Chimera::VulkanUtils {

    VkDescriptorImageInfo DescriptorImageInfo(VkImageView view, VkImageLayout layout, VkSampler sampler);
    
    VkDescriptorSetLayoutBinding DescriptorSetLayoutBinding(
        uint32_t binding, 
        VkDescriptorType descriptorType, 
        VkShaderStageFlags stageFlags, 
        uint32_t descriptorCount = 1
    );
}
