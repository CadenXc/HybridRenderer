#include "pch.h"
#include "Image.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera
{
    Image::Image(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspectFlags, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkImageTiling tiling)
        : m_Width(width), m_Height(height), m_Format(format), m_MipLevels(mipLevels)
    {
        m_Device = VulkanContext::Get().GetDevice();
        m_Allocator = VulkanContext::Get().GetAllocator();

        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiling;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = numSamples;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(m_Allocator, &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create image!");
        }

        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_View) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create image view!");
        }
    }

    Image::~Image()
    {
        if (m_View != VK_NULL_HANDLE) vkDestroyImageView(m_Device, m_View, nullptr);
        if (m_Image != VK_NULL_HANDLE) vmaDestroyImage(m_Allocator, m_Image, m_Allocation);
    }

    Image::Image(Image&& other) noexcept
        : m_Device(other.m_Device), m_Allocator(other.m_Allocator), m_Image(other.m_Image), m_Allocation(other.m_Allocation), m_View(other.m_View), m_Width(other.m_Width), m_Height(other.m_Height), m_Format(other.m_Format), m_MipLevels(other.m_MipLevels)
    {
        other.m_Image = VK_NULL_HANDLE; other.m_Allocation = VK_NULL_HANDLE; other.m_View = VK_NULL_HANDLE;
    }

    Image& Image::operator=(Image&& other) noexcept
    {
        if (this != &other)
        {
            if (m_View != VK_NULL_HANDLE) vkDestroyImageView(m_Device, m_View, nullptr);
            if (m_Image != VK_NULL_HANDLE) vmaDestroyImage(m_Allocator, m_Image, m_Allocation);
            m_Device = other.m_Device; m_Allocator = other.m_Allocator; m_Image = other.m_Image; m_Allocation = other.m_Allocation; m_View = other.m_View; m_Width = other.m_Width; m_Height = other.m_Height; m_Format = other.m_Format; m_MipLevels = other.m_MipLevels;
            other.m_Image = VK_NULL_HANDLE; other.m_Allocation = VK_NULL_HANDLE; other.m_View = VK_NULL_HANDLE;
        }
        return *this;
    }
}
