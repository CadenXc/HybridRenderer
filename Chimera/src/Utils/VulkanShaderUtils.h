#pragma once
#include "pch.h"

namespace Chimera
{
    class VulkanContext;

    namespace VulkanUtils
    {
        VkShaderModule LoadShaderModule(const std::string& filename, VkDevice device);

        void CopyBuffer(
            VkBuffer srcBuffer,
            VkBuffer dstBuffer,
            VkDeviceSize size
        );

        // [NEW] Debug naming support
        void SetDebugUtilsObjectName(VkDevice device, VkObjectType type, uint64_t handle, const char* name);
    }
}