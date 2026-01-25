#pragma once

#include "pch.h"
#include "VulkanContext.h"
#include "Image.h"
#include "Buffer.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>

namespace Chimera {

    struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 lightPos;
        int frameCount;
        int padding[3];
    };

    class ResourceManager {
    public:
        ResourceManager(std::shared_ptr<VulkanContext> context);
        ~ResourceManager();

        std::unique_ptr<Image> LoadTexture(const std::string& path);
        VkSampler GetTextureSampler() const { return m_TextureSampler; }

        // Global Resources
        void InitGlobalResources(); // Creates UBOs, Descriptors
        void UpdateGlobalResources(uint32_t currentFrame, const UniformBufferObject& ubo);
        
        VkDescriptorSet GetGlobalDescriptorSet(uint32_t currentFrame) const { return m_DescriptorSets[currentFrame]; }
        VkDescriptorSetLayout GetGlobalDescriptorSetLayout() const { return m_DescriptorSetLayout; }

    private:
        void CreateTextureSampler();
        void CreateDescriptorSetLayout();
        void CreateUniformBuffers();
        void CreateDescriptorPool();
        void CreateDescriptorSets();
        
        // Helper command functions (similar to what was in Application)
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
        void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
        void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
        void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

    private:
        std::shared_ptr<VulkanContext> m_Context;
        VkSampler m_TextureSampler = VK_NULL_HANDLE;

        // Global Resources
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        std::vector<std::unique_ptr<Buffer>> m_UniformBuffers;
        std::vector<VkDescriptorSet> m_DescriptorSets;
        
        // Default Texture (e.g. Viking Room texture used globally for now)
        // Ideally Scene holds materials, but we are refactoring incrementally.
        // For now, ResourceManager holds "Global" texture.
        std::unique_ptr<Image> m_GlobalTexture;
    };

}
