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
        std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
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

        // --- Feature Chain Construction ---
        
        // 1. Descriptor Indexing
        VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexing{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
        descriptorIndexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        descriptorIndexing.runtimeDescriptorArray = VK_TRUE;
        descriptorIndexing.descriptorBindingVariableDescriptorCount = VK_TRUE;
        descriptorIndexing.descriptorBindingPartiallyBound = VK_TRUE;
        descriptorIndexing.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
        descriptorIndexing.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
        descriptorIndexing.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;

        // 2. Buffer Device Address
        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
        bufferDeviceAddress.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddress.bufferDeviceAddress = VK_TRUE;
        bufferDeviceAddress.pNext = &descriptorIndexing;

        void* pNextChain = &bufferDeviceAddress;

        // 3. Ray Tracing (Optional)
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
        VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
        if (m_RayTracingSupported)
        {
            rtPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
            rtPipelineFeatures.rayTracingPipeline = VK_TRUE;
            rtPipelineFeatures.pNext = pNextChain;
            
            asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            asFeatures.accelerationStructure = VK_TRUE;
            asFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;
            asFeatures.pNext = &rtPipelineFeatures;
            pNextChain = &asFeatures;
        }

        // 4. Scalar Layout
        VkPhysicalDeviceScalarBlockLayoutFeatures scalarLayout{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES };
        scalarLayout.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
        scalarLayout.scalarBlockLayout = VK_TRUE;
        scalarLayout.pNext = pNextChain;

        // 5. Vulkan 1.3 Core Features (including Dynamic Rendering and Sync 2)
        VkPhysicalDeviceVulkan13Features vulkan13Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkan13Features.dynamicRendering = VK_TRUE;
        vulkan13Features.synchronization2 = VK_TRUE;
        vulkan13Features.shaderDemoteToHelperInvocation = VK_TRUE;
        vulkan13Features.pNext = &scalarLayout;

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

        vkGetDeviceQueue(m_LogicalDevice, indices.graphicsFamily.value(), 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_LogicalDevice, indices.presentFamily.value(), 0, &m_PresentQueue);
        m_GraphicsQueueFamily = indices.graphicsFamily.value();
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
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport)
            {
                indices.presentFamily = i;
            }
            if (indices.isComplete())
            {
                break;
            }
        }
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
