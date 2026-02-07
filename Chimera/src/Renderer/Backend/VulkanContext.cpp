#include "pch.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera {

    VulkanContext::VulkanContext(GLFWwindow* window)
        : m_Window(window)
    {
        CH_CORE_INFO("VulkanContext: Initializing Instance...");
        m_Instance = std::make_unique<VulkanInstance>("Chimera Engine");

        CH_CORE_INFO("VulkanContext: Creating Surface...");
        CreateSurface();

        CH_CORE_INFO("VulkanContext: Initializing Device...");
        m_Device = std::make_unique<VulkanDevice>(m_Instance->GetHandle(), m_Surface);

        CH_CORE_INFO("VulkanContext: Creating Command Pool...");
        CreateCommandPool();

        CH_CORE_INFO("VulkanContext: Initializing Swapchain...");
        m_Swapchain = std::make_shared<Swapchain>(GetDevice(), GetPhysicalDevice(), m_Surface, m_Window);

        CH_CORE_INFO("VulkanContext Initialized.");
    }

    VulkanContext::~VulkanContext()
    {
        m_Swapchain.reset();
        if (m_CommandPool != VK_NULL_HANDLE) vkDestroyCommandPool(GetDevice(), m_CommandPool, nullptr);
        
        m_Device.reset();
        
        if (m_Surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(m_Instance->GetHandle(), m_Surface, nullptr);
        
        m_Instance.reset();
    }

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

}
