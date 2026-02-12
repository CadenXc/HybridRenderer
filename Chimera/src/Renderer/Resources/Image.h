#pragma once

#include "pch.h"
#include "vk_mem_alloc.h"

namespace Chimera
{
    class Image 
    {
    public:
        Image(uint32_t width, uint32_t height, 
              VkFormat format, VkImageUsageFlags usage, 
              VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT,
              uint32_t mipLevels = 1,
              VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT,
              VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL);

        ~Image();

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

        Image(Image&& other) noexcept;
        Image& operator=(Image&& other) noexcept;

        VkImage GetImage() const { return m_Image; }
        VkImageView GetImageView() const { return m_View; }
        VkFormat GetFormat() const { return m_Format; }
        VkExtent2D GetExtent() const { return { m_Width, m_Height }; }
        uint32_t GetMipLevels() const { return m_MipLevels; }

    private:
        VkDevice m_Device = VK_NULL_HANDLE;
        VmaAllocator m_Allocator = nullptr;
        VkImage m_Image = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkImageView m_View = VK_NULL_HANDLE;

        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        VkFormat m_Format = VK_FORMAT_UNDEFINED;
        uint32_t m_MipLevels = 1;
    };
}