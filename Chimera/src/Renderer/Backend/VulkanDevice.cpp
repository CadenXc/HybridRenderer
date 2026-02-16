#include "pch.h"
#include "VulkanDevice.h"
#include "Swapchain.h"

namespace Chimera
{
    static const char* requiredDeviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
    };

    static const char* optionalDeviceExtensions[] = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    };

    VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface)
    {
        PickPhysicalDevice(instance, surface);
        CreateLogicalDevice(surface);
        volkLoadDevice(m_LogicalDevice);
        CreateAllocator(instance);
    }

    VulkanDevice::~VulkanDevice()
    {
        CH_CORE_INFO("VulkanDevice: Finalizing device destruction...");
        
        vmaDestroyAllocator(m_Allocator);
        vkDestroyDevice(m_LogicalDevice, nullptr);
    }

    void VulkanDevice::PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface)
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        if (deviceCount == 0)
        {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

        std::multimap<int, VkPhysicalDevice> candidates;
        for (const auto& device : devices)
        {
            candidates.insert(std::make_pair(RateDeviceSuitability(device, surface), device));
        }

        if (candidates.rbegin()->first > 0)
        {
            m_PhysicalDevice = candidates.rbegin()->second;
            vkGetPhysicalDeviceProperties(m_PhysicalDevice, &m_DeviceProperties);
            
            uint32_t extensionCount;
            vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> availableExtensions(extensionCount);
            vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, availableExtensions.data());
            
            std::set<std::string> optional;
            for (auto ext : optionalDeviceExtensions)
            {
                optional.insert(ext);
            }
            for (const auto& ext : availableExtensions)
            {
                optional.erase(ext.extensionName);
            }
            m_RayTracingSupported = optional.empty();

            CH_CORE_INFO("Selected GPU: {}", m_DeviceProperties.deviceName);
            CH_CORE_INFO("Hardware Ray Tracing Support: {}", m_RayTracingSupported ? "YES" : "NO");

            if (m_RayTracingSupported)
            {
                m_RayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
                VkPhysicalDeviceProperties2 prop2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
                prop2.pNext = &m_RayTracingProperties;
                vkGetPhysicalDeviceProperties2(m_PhysicalDevice, &prop2);
            }
        }
        else
        {
            throw std::runtime_error("failed to find a suitable GPU!");
        }
    }

    void VulkanDevice::CreateLogicalDevice(VkSurfaceKHR surface)
    {
        QueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice, surface);
        
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = { 
            indices.graphicsFamily.value(), 
            indices.computeFamily.value(), 
            indices.presentFamily.value() 
        };

        float queuePriorities[] = { 1.0f, 1.0f }; // Request up to 2 queues if in same family
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queueCreateInfo.queueFamilyIndex = queueFamily;
            
            // If compute and graphics are in the same family, we try to get 2 queues for async overlap
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &count, families.data());
            
            uint32_t requestedCount = 1;
            if (queueFamily == indices.graphicsFamily && queueFamily == indices.computeFamily && families[queueFamily].queueCount > 1)
                requestedCount = 2;

            queueCreateInfo.queueCount = requestedCount;
            queueCreateInfo.pQueuePriorities = queuePriorities;
            queueCreateInfos.push_back(queueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = VK_TRUE;
        deviceFeatures.shaderInt64 = VK_TRUE;

        std::vector<const char*> enabledExtensions;
        for (auto ext : requiredDeviceExtensions)
        {
            enabledExtensions.push_back(ext);
        }
        if (m_RayTracingSupported)
        {
            for (auto ext : optionalDeviceExtensions)
            {
                enabledExtensions.push_back(ext);
            }
        }

        // --- Feature Chain Construction (Spec Compliant) ---
        VkPhysicalDeviceVulkan12Features vulkan12Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        vulkan12Features.bufferDeviceAddress = VK_TRUE;
        vulkan12Features.descriptorIndexing = VK_TRUE;
        vulkan12Features.scalarBlockLayout = VK_TRUE;
        vulkan12Features.hostQueryReset = VK_TRUE;
        vulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        vulkan12Features.runtimeDescriptorArray = VK_TRUE;
        vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
        vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
        vulkan12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        vulkan12Features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
        rtPipelineFeatures.rayTracingPipeline = VK_TRUE;

        VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
        asFeatures.accelerationStructure = VK_TRUE;
        asFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;

        VkPhysicalDeviceVulkan13Features vulkan13Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        vulkan13Features.dynamicRendering = VK_TRUE;
        vulkan13Features.synchronization2 = VK_TRUE;
        vulkan13Features.shaderDemoteToHelperInvocation = VK_TRUE;

        // Build Chain: 13 -> 12 -> RT -> AS
        vulkan13Features.pNext = &vulkan12Features;
        vulkan12Features.pNext = &rtPipelineFeatures;
        rtPipelineFeatures.pNext = &asFeatures;
        asFeatures.pNext = nullptr;

        VkDeviceCreateInfo createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
        createInfo.ppEnabledExtensionNames = enabledExtensions.data();
        createInfo.pNext = &vulkan13Features;

        if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_LogicalDevice) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create logical device!");
        }

        m_GraphicsQueueFamily = indices.graphicsFamily.value();
        m_ComputeQueueFamily = indices.computeFamily.value();

        vkGetDeviceQueue(m_LogicalDevice, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
        
        // If same family and multiple queues requested, take index 1 for compute
        if (m_GraphicsQueueFamily == m_ComputeQueueFamily) {
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &count, families.data());
            
            if (families[m_GraphicsQueueFamily].queueCount > 1)
                vkGetDeviceQueue(m_LogicalDevice, m_ComputeQueueFamily, 1, &m_ComputeQueue);
            else
                m_ComputeQueue = m_GraphicsQueue;
        } else {
            vkGetDeviceQueue(m_LogicalDevice, m_ComputeQueueFamily, 0, &m_ComputeQueue);
        }

        vkGetDeviceQueue(m_LogicalDevice, indices.presentFamily.value(), 0, &m_PresentQueue);
    }

    void VulkanDevice::CreateAllocator(VkInstance instance)
    {
        VmaVulkanFunctions vmaFuncs{};
        vmaFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vmaFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        vmaFuncs.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
        vmaFuncs.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
        vmaFuncs.vkAllocateMemory = vkAllocateMemory;
        vmaFuncs.vkFreeMemory = vkFreeMemory;
        vmaFuncs.vkMapMemory = vkMapMemory;
        vmaFuncs.vkUnmapMemory = vkUnmapMemory;
        vmaFuncs.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
        vmaFuncs.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
        vmaFuncs.vkBindBufferMemory = vkBindBufferMemory;
        vmaFuncs.vkBindImageMemory = vkBindImageMemory;
        vmaFuncs.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
        vmaFuncs.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
        vmaFuncs.vkCreateBuffer = vkCreateBuffer;
        vmaFuncs.vkDestroyBuffer = vkDestroyBuffer;
        vmaFuncs.vkCreateImage = vkCreateImage;
        vmaFuncs.vkDestroyImage = vkDestroyImage;
        vmaFuncs.vkCmdCopyBuffer = vkCmdCopyBuffer;
        vmaFuncs.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
        vmaFuncs.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
        vmaFuncs.vkBindBufferMemory2KHR = vkBindBufferMemory2;
        vmaFuncs.vkBindImageMemory2KHR = vkBindImageMemory2;
        vmaFuncs.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
        vmaFuncs.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements;
        vmaFuncs.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements;

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = m_PhysicalDevice;
        allocatorInfo.device = m_LogicalDevice;
        allocatorInfo.instance = instance;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        allocatorInfo.pVulkanFunctions = &vmaFuncs;

        if (vmaCreateAllocator(&allocatorInfo, &m_Allocator) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create VMA allocator!");
        }
    }

    QueueFamilyIndices VulkanDevice::FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
    {
        QueueFamilyIndices indices;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
        
        for (int i = 0; i < (int)count; ++i)
        {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.graphicsFamily = i;
            }
            
            // Prefer a dedicated compute queue (one that doesn't have graphics bit)
            if ((families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                indices.computeFamily = i;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport)
            {
                indices.presentFamily = i;
            }
            
            if (indices.graphicsFamily.has_value() && indices.presentFamily.has_value() && indices.computeFamily.has_value())
                break;
        }

        // If no dedicated compute queue, use the graphics one
        if (!indices.computeFamily.has_value())
            indices.computeFamily = indices.graphicsFamily;

        return indices;
    }

    int VulkanDevice::RateDeviceSuitability(VkPhysicalDevice device, VkSurfaceKHR surface)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ? 1000 : 0;
        QueueFamilyIndices indices = FindQueueFamilies(device, surface);
        if (!indices.isComplete())
        {
            return 0;
        }
        return score + 1;
    }

    uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }
        throw std::runtime_error("failed to find suitable memory type!");
    }

    VkFormat VulkanDevice::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
    {
        for (VkFormat format : candidates)
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &props);
            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }
        throw std::runtime_error("failed to find supported format!");
    }
}
