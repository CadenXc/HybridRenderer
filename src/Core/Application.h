#pragma once

#include "pch.h"
#include "VulkanContext.h"
#include "Renderer.h"
#include "Scene.h"
#include "Layer.h"
#include "Buffer.h"
#include "Image.h"
#include "CameraController.h"
#include "ResourceManager.h"

namespace Chimera {

    class Buffer;
    class Image;
    // Vertex struct moved to Scene.h

    struct UniformBufferObject {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 lightPos;
        int frameCount;
        int padding[3];
    };

    struct FrameData {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
    };

    class Application {
    public:
        Application();
        virtual ~Application();

        void Run();
        void PushLayer(const std::shared_ptr<Layer>& layer);

        // Expose Context and Renderer to Layers (for now, until we abstract further)
        std::shared_ptr<VulkanContext> GetContext() { return m_Context; }
        std::shared_ptr<Renderer> GetRenderer() { return m_Renderer; }

    private:
        void initWindow();
        void initVulkan();
        void mainLoop();
        void cleanup();

        void cleanupSwapChain();
        
        void createDescriptorSetLayout();
        void createGraphicsPipeline();
        // void createFramebuffers(); // Removed for Dynamic Rendering
        void createCommandPool();
        void createColorResources();
        void createDepthResources();
        
        void createUniformBuffers();
        void createDescriptorPool();
        void createDescriptorSets();
        // void createFrameResources(); // Moved to Renderer
        // void createCommandBuffers();
        // void createSyncObjects();

        // Ray Tracing
        void createTopLevelAS();
        void createStorageImage();
        void createAccumulationImage();
        void createRayTracingDescriptorSetLayout();
        void createRayTracingDescriptorSets();
        void createRayTracingPipeline();
        void createShaderBindingTable();

        void drawFrame();
        void updateUniformBuffer(uint32_t currentImage);
        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        VkShaderModule createShaderModule(const std::vector<char>& code);
        
        VkFormat findDepthFormat();
        
        bool hasStencilComponent(VkFormat format);
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);

        VkTransformMatrixKHR toVkMatrix(glm::mat4 model);
        uint64_t getAccelerationStructureDeviceAddress(VkAccelerationStructureKHR as);
        uint32_t align_up(uint32_t value, uint32_t alignment);

        static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
        static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);

    private:
        GLFWwindow* window;
        std::shared_ptr<VulkanContext> m_Context;
        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<Scene> m_Scene;
        std::unique_ptr<CameraController> m_CameraController;
        std::unique_ptr<ResourceManager> m_ResourceManager;

        // Pipeline & RenderPass
        VkDescriptorSetLayout descriptorSetLayout;
        VkPipelineLayout pipelineLayout;
        VkPipeline graphicsPipeline;

        // Command Pool & Buffers
        // VkCommandPool commandPool; // Managed by Context/Renderer now

        // Sync Objects - Moved to Renderer
        // std::vector<FrameData> m_Frames;
        // std::vector<VkFence> imagesInFlight;
        size_t currentFrame = 0;

        bool framebufferResized = false;

        // Geometry members moved to Scene
        
        // Layers
        float m_LastFrameTime = 0.0f;
        std::vector<std::shared_ptr<Layer>> m_LayerStack;

        // RT Structures
        VkAccelerationStructureKHR topLevelAS;
        std::unique_ptr<Buffer> tlasBuffer;

        // Uniforms & Descriptors
        std::vector<std::unique_ptr<Buffer>> uniformBuffers;
        VkDescriptorPool descriptorPool;
        std::vector<VkDescriptorSet> descriptorSets;

        // Textures
        std::unique_ptr<Image> textureImage;

        // Attachments
        std::unique_ptr<Image> depthImage;
        std::unique_ptr<Image> colorImage;

        // RT Resources
        std::unique_ptr<Image> storageImage;
        VkFormat storageImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
        std::unique_ptr<Image> accumulationImage;

        VkDescriptorSetLayout rtDescriptorSetLayout;
        VkDescriptorPool rtDescriptorPool;
        std::vector<VkDescriptorSet> rtDescriptorSets;
        VkPipeline rayTracingPipeline;
        VkPipelineLayout rayTracingPipelineLayout;
        std::unique_ptr<Buffer> sbtBuffer;

        VkStridedDeviceAddressRegionKHR rgenRegion{};
        VkStridedDeviceAddressRegionKHR missRegion{};
        VkStridedDeviceAddressRegionKHR hitRegion{};
        VkStridedDeviceAddressRegionKHR callRegion{};
    };

    Application* CreateApplication(int argc, char** argv);
}
