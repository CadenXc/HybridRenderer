#pragma once

// 1. 宏定义 (必须在包含 Vulkan/GLFW 之前)
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

// 2. 核心库包含
#include <volk.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include "vk_mem_alloc.h" // VMA 头文件

// 3. 标准库包含
#include <vector>
#include <optional>
#include <string>
#include <array>
#include <set>
#include <iostream>
#include <fstream>
#include <chrono>

#include <memory>
#include "Layer.h"

namespace Chimera {

    // ==============================================================================
    // 辅助结构体定义 (Vertex, UBO 等)
    // ==============================================================================

    struct Vertex 
    {
        glm::vec3 pos;
        glm::vec3 color;
        glm::vec2 texCoord;

        bool operator==(const Vertex& other) const 
        {
            return pos == other.pos && color == other.color && texCoord == other.texCoord;
        }

        static VkVertexInputBindingDescription getBindingDescription() 
        {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() 
        {
            std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, color);

            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

            return attributeDescriptions;
        }
    };

    struct UniformBufferObject 
    {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 lightPos;
        int frameCount; // 确保这里有 frameCount，因为 Shader 用到了
        int padding[3];
    };

    struct QueueFamilyIndices 
    {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() 
        {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SwapChainSupportDetails 
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    // ==============================================================================
    // Application 类定义
    // ==============================================================================

    class Application
    {
    public:
        Application();
        virtual ~Application();

        void Run();

        void PushLayer(const std::shared_ptr<Layer>& layer);

    private:
        // 初始化流程
        void initWindow();
        void initVulkan();
        void mainLoop();
        void cleanup();

        // 核心 Vulkan 创建函数
        void createInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createSwapChain();
        void recreateSwapChain();
        void cleanupSwapChain();
        void createImageViews();
        void createRenderPass();
        void createDescriptorSetLayout();
        void createGraphicsPipeline();
        void createFramebuffers();
        void createCommandPool();
        void createColorResources();
        void createDepthResources();
        void createTextureImage();
        void createTextureImageView();
        void createTextureSampler();
        void loadModel();
        void createVertexBuffer();
        void createIndexBuffer();
        void createUniformBuffers();
        void createDescriptorPool();
        void createDescriptorSets();
        void createCommandBuffers();
        void createSyncObjects();
        
        // Ray Tracing 相关
        void createBottomLevelAS();
        void createTopLevelAS();
        void createStorageImage();
        void createAccumulationImage(); 
        void createRayTracingDescriptorSetLayout();
        void createRayTracingDescriptorSets();
        void createRayTracingPipeline();
        void createShaderBindingTable();

        // 渲染与逻辑
        void drawFrame();
        void updateUniformBuffer(uint32_t currentImage);
        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        // 辅助函数
        VkShaderModule createShaderModule(const std::vector<char>& code);
        bool checkValidationLayerSupport();
        std::vector<const char*> getRequiredExtensions();
        int rateDeviceSuitability(VkPhysicalDevice device);
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
        VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
        VkFormat findDepthFormat();
        bool hasStencilComponent(VkFormat format);
        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VkBuffer& buffer, VmaAllocation& allocation);
        void createImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VmaAllocation& allocation);
        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);
        void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
        void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
        void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
        
        VkTransformMatrixKHR toVkMatrix(glm::mat4 model);
        uint64_t getAccelerationStructureDeviceAddress(VkAccelerationStructureKHR as);
        uint64_t getBufferDeviceAddress(VkBuffer buffer);
        uint32_t align_up(uint32_t value, uint32_t alignment);

        // [新增] 缺失的函数声明
        VkSampleCountFlagBits getMaxUsableSampleCount();
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

        // 静态回调
        static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    private:

        GLFWwindow* window;
        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkSurfaceKHR surface;

        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device;
        VkQueue graphicsQueue;
        VkQueue presentQueue;
        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocator allocator;

        VkSwapchainKHR swapChain;
        std::vector<VkImage> swapChainImages;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        std::vector<VkImageView> swapChainImageViews;
        std::vector<VkFramebuffer> swapChainFramebuffers;

        VkRenderPass renderPass;
        VkDescriptorSetLayout descriptorSetLayout;
        VkPipelineLayout pipelineLayout;
        VkPipeline graphicsPipeline;

        VkCommandPool commandPool;
        std::vector<VkCommandBuffer> commandBuffers;
        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        std::vector<VkFence> imagesInFlight;
        size_t currentFrame = 0;

        bool framebufferResized = false;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        VkBuffer vertexBuffer;
        VmaAllocation vertexBufferAllocation;

        VkBuffer indexBuffer;
        VmaAllocation indexBufferAllocation;

        // Layer相关
        float m_LastFrameTime = 0.0f;
        std::vector<std::shared_ptr<Layer>> m_LayerStack;

        // RT AS
        VkAccelerationStructureKHR bottomLevelAS;
        VkBuffer blasBuffer;
        VmaAllocation blasBufferAllocation;

        VkAccelerationStructureKHR topLevelAS;
        VkBuffer tlasBuffer;
        VmaAllocation tlasBufferAllocation;

        // Buffers & Descriptors
        std::vector<VkBuffer> uniformBuffers;
        std::vector<VmaAllocation> uniformBuffersAllocation;
        std::vector<void*> uniformBuffersMapped;

        VkDescriptorPool descriptorPool;
        std::vector<VkDescriptorSet> descriptorSets;

        uint32_t mipLevels;

        // Images
        VkImage textureImage;
        VmaAllocation textureImageAllocation;
        VkImageView textureImageView;
        VkSampler textureSampler;

        VkImage depthImage;
        VmaAllocation depthImageAllocation;
        VkImageView depthImageView;

        VkImage colorImage;
        VmaAllocation colorImageAllocation;
        VkImageView colorImageView;

        // RT Images
        VkImage storageImage;
        VmaAllocation storageImageAllocation;
        VkImageView storageImageView;
        VkFormat storageImageFormat = VK_FORMAT_R8G8B8A8_UNORM;

        VkImage accumulationImage;
        VmaAllocation accumulationImageAllocation;
        VkImageView accumulationImageView;

        // RT Descriptors & Pipeline
        VkDescriptorSetLayout rtDescriptorSetLayout;
        VkDescriptorPool rtDescriptorPool;
        std::vector<VkDescriptorSet> rtDescriptorSets;

        VkPipeline rayTracingPipeline;
        VkPipelineLayout rayTracingPipelineLayout;

        VkBuffer sbtBuffer;
        VmaAllocation sbtBufferAllocation;

        VkStridedDeviceAddressRegionKHR rgenRegion{};
        VkStridedDeviceAddressRegionKHR missRegion{};
        VkStridedDeviceAddressRegionKHR hitRegion{};
        VkStridedDeviceAddressRegionKHR callRegion{};
    };

    // 工厂函数：由客户端实现
    Application* CreateApplication(int argc, char** argv);
}

// Hash 扩展放在 namespace 外面
namespace std {
    template<> struct hash<Chimera::Vertex> {
        size_t operator()(Chimera::Vertex const& vertex) const {
            return ((hash<glm::vec3>()(vertex.pos) ^
                (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
                (hash<glm::vec2>()(vertex.texCoord) << 1);
        }
    };
}