#pragma once
#include "pch.h"

namespace Chimera {

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
    };

    class VulkanDevice {
    public:
        VulkanDevice(VkInstance instance, VkSurfaceKHR surface);
        ~VulkanDevice();

        VkDevice GetHandle() const { return m_LogicalDevice; }
        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
        VkQueue GetPresentQueue() const { return m_PresentQueue; }
        VmaAllocator GetAllocator() const { return m_Allocator; }
        
        bool IsRayTracingSupported() const { return m_RayTracingSupported; }
        const VkPhysicalDeviceProperties& GetProperties() const { return m_DeviceProperties; }
        const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& GetRTProperties() const { return m_RayTracingProperties; }
        VkSampleCountFlagBits GetMaxUsableSampleCount() const { return m_MaxSamples; }

        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    private:
        void PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);
        void CreateLogicalDevice(VkSurfaceKHR surface);
        void CreateAllocator(VkInstance instance);
        
        int RateDeviceSuitability(VkPhysicalDevice device, VkSurfaceKHR surface);
        bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
        VkSampleCountFlagBits QueryMaxUsableSampleCount();

    private:
        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice m_LogicalDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties m_DeviceProperties{};
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_RayTracingProperties{};
        
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue m_PresentQueue = VK_NULL_HANDLE;
        uint32_t m_GraphicsQueueFamily = 0;
        
        VmaAllocator m_Allocator = VK_NULL_HANDLE;
        VkSampleCountFlagBits m_MaxSamples = VK_SAMPLE_COUNT_1_BIT;
        bool m_RayTracingSupported = false;
    };

}
