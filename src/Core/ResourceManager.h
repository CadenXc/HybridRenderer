#pragma once

#include "pch.h"
#include "VulkanContext.h"
#include "Image.h"
#include <string>
#include <memory>
#include <unordered_map>

namespace Chimera {

    class ResourceManager {
    public:
        ResourceManager(std::shared_ptr<VulkanContext> context);
        ~ResourceManager();

        std::unique_ptr<Image> LoadTexture(const std::string& path);
        VkSampler GetTextureSampler() const { return m_TextureSampler; }

    private:
        void CreateTextureSampler();
        
        // Helper command functions (similar to what was in Application)
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
        void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
        void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
        void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

    private:
        std::shared_ptr<VulkanContext> m_Context;
        VkSampler m_TextureSampler;
    };

}
