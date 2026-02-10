#include "pch.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera
{
    VulkanContext* VulkanContext::s_Instance = nullptr;

    VulkanContext::VulkanContext(GLFWwindow* window)
        : m_Window(window)
    {
        s_Instance = this;
        CH_CORE_INFO("VulkanContext: Creating core Vulkan link...");

        // 1. Core Connection
        m_Instance = std::make_unique<VulkanInstance>("Chimera Engine");
        CreateSurface();

        // 2. Logical Device & Features
        m_Device = std::make_unique<VulkanDevice>(m_Instance->GetHandle(), m_Surface);

        // 3. Command & Swap Infrastructure
        CreateCommandPool();
        m_DeletionQueue.Init(3); // Match MAX_FRAMES_IN_FLIGHT
        m_Swapchain = std::make_shared<Swapchain>(GetDevice(), GetPhysicalDevice(), m_Surface, m_Window);

        // 4. Common Resources
        CreateEmptyLayout();

        CH_CORE_INFO("VulkanContext Initialized.");
    }

    VulkanContext::~VulkanContext()
    {
        m_DeletionQueue.FlushAll();
        m_Swapchain.reset();

        if (m_EmptyDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(GetDevice(), m_EmptyDescriptorSetLayout, nullptr);
        }
        if (m_CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(GetDevice(), m_CommandPool, nullptr);
        }
        
        m_Device.reset();
        
        if (m_Surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_Instance->GetHandle(), m_Surface, nullptr);
        }
        
        m_Instance.reset();
        s_Instance = nullptr;
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // [Initialization Subroutines]
    // ---------------------------------------------------------------------------------------------------------------------

    void VulkanContext::CreateSurface()
    {
        if (glfwCreateWindowSurface(m_Instance->GetHandle(), m_Window, nullptr, &m_Surface) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create window surface!");
        }
    }

    void VulkanContext::CreateCommandPool()
    {
        QueueFamilyIndices indices = m_Device->FindQueueFamilies(GetPhysicalDevice(), m_Surface);

        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

        if (vkCreateCommandPool(GetDevice(), &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create command pool!");
        }
    }

    void VulkanContext::CreateEmptyLayout()
    {
        // An empty layout is required for pipelines that don't use pass-specific descriptors
        // but must adhere to the 3-Set Contract (Set 0 Global, Set 1 Scene, Set 2 Pass).
        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        if (vkCreateDescriptorSetLayout(GetDevice(), &layoutInfo, nullptr, &m_EmptyDescriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create empty descriptor set layout!");
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------
    // [Utility Methods]
    // ---------------------------------------------------------------------------------------------------------------------

    VkImageView VulkanContext::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
    {
        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        if (vkCreateImageView(GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create image view!");
        }
        return imageView;
    }

    void VulkanContext::SetDebugName(uint64_t handle, VkObjectType type, const char* name)
    {
        if (handle == 0 || name == nullptr)
        {
            return;
        }

        auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(GetDevice(), "vkSetDebugUtilsObjectNameEXT");
        if (func)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
            nameInfo.objectType = type;
            nameInfo.objectHandle = handle;
            nameInfo.pObjectName = name;
            func(GetDevice(), &nameInfo);
        }
    }
}
