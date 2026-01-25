#pragma once

#include "pch.h"
#include "VulkanContext.h"
#include "ResourceManager.h"
#include "Buffer.h"
#include "Image.h"

namespace Chimera {

    // Forward declarations
    class Application;

    // Context for rendering a frame
    struct RenderContext {
        VkCommandBuffer commandBuffer;
        uint32_t frameIndex;
        uint32_t imageIndex;
    };

    /**
     * RayTracingPass: Encapsulates all ray tracing pipeline logic
     * 
     * Responsibilities:
     * - Create and manage ray tracing pipeline
     * - Create and manage shader binding table
     * - Create and manage RT-specific descriptor sets
     * - Execute ray tracing commands
     */
    class RayTracingPass {
    public:
        RayTracingPass(std::shared_ptr<VulkanContext> context, ResourceManager* resourceManager);
        ~RayTracingPass();

        // Initialize the pass with acceleration structures and resources
        void Init(VkAccelerationStructureKHR topLevelAS, Image* storageImage,
                  VkDescriptorSetLayout rtDescriptorSetLayout, VkDescriptorSetLayout graphicsDescriptorSetLayout);

        // Execute ray tracing for a frame
        void Execute(VkCommandBuffer cmd, const RenderContext& renderContext, 
                     const std::vector<VkDescriptorSet>& rtDescriptorSets,
                     VkDescriptorSet graphicsDescriptorSet);

        // Handle window resize
        void OnResize(uint32_t width, uint32_t height);

        // Update image layout tracking (called after copy operations in Application)
        void SetStorageImageLayout(VkImageLayout layout) { m_StorageImageLayout = layout; }

        // Getters
        VkPipeline GetPipeline() const { return m_RayTracingPipeline; }
        VkPipelineLayout GetPipelineLayout() const { return m_RayTracingPipelineLayout; }
        VkStridedDeviceAddressRegionKHR GetRaygenRegion() const { return m_RaygenRegion; }
        VkStridedDeviceAddressRegionKHR GetMissRegion() const { return m_MissRegion; }
        VkStridedDeviceAddressRegionKHR GetHitRegion() const { return m_HitRegion; }
        VkStridedDeviceAddressRegionKHR GetCallableRegion() const { return m_CallableRegion; }

    private:
        void CreateRayTracingPipeline(VkDescriptorSetLayout rtDescriptorSetLayout, 
                                      VkDescriptorSetLayout graphicsDescriptorSetLayout);
        void CreateShaderBindingTable(uint32_t groupCount);

        // Helper functions
        VkShaderModule LoadShaderModule(const std::vector<char>& code);
        uint32_t align_up(uint32_t value, uint32_t alignment);
        uint64_t getAccelerationStructureDeviceAddress(VkAccelerationStructureKHR as);

    private:
        std::shared_ptr<VulkanContext> m_Context;
        ResourceManager* m_ResourceManager = nullptr;  // Non-owning pointer

        // Pipeline and layout
        VkPipeline m_RayTracingPipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_RayTracingPipelineLayout = VK_NULL_HANDLE;

        // Shader binding table
        std::unique_ptr<Buffer> m_ShaderBindingTableBuffer;
        VkStridedDeviceAddressRegionKHR m_RaygenRegion{};
        VkStridedDeviceAddressRegionKHR m_MissRegion{};
        VkStridedDeviceAddressRegionKHR m_HitRegion{};
        VkStridedDeviceAddressRegionKHR m_CallableRegion{};

        // Resources
        VkAccelerationStructureKHR m_TopLevelAS = VK_NULL_HANDLE;
        Image* m_StorageImage = nullptr;  // Non-owning pointer to Image
        VkImageLayout m_StorageImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // Properties
        uint32_t m_WindowWidth = 800;
        uint32_t m_WindowHeight = 600;
    };

}
