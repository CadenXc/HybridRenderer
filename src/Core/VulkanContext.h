#pragma once
#include "pch.h"

namespace Chimera {

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

    class VulkanContext
    {
    public:
        VulkanContext(GLFWwindow* window);
        ~VulkanContext();

        VulkanContext(const VulkanContext&) = delete;
        VulkanContext& operator=(const VulkanContext&) = delete;

        GLFWwindow* GetWindow() const { return m_Window; }
        VkDevice GetDevice() const { return device; }
        VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
        VkInstance GetInstance() const { return instance; }
        VkQueue GetGraphicsQueue() const { return graphicsQueue; }
        uint32_t GetGraphicsQueueFamily() const { return graphicsQueueFamily; }
        VkQueue GetPresentQueue() const { return presentQueue; }
        VkSurfaceKHR GetSurface() const { return surface; }
        VmaAllocator GetAllocator() const { return allocator; }
        VkCommandPool GetCommandPool() const { return commandPool; }
        VkSampleCountFlagBits GetMSAASamples() const { return msaaSamples; }

        // Swapchain
        void CreateSwapChain();
        void RecreateSwapChain();
        void CleanupSwapChain();
        
        VkSwapchainKHR GetSwapChain() const { return swapChain; }
        uint32_t GetSwapChainImageCount() const { return static_cast<uint32_t>(swapChainImages.size()); }
        VkFormat GetSwapChainImageFormat() const { return swapChainImageFormat; }
        VkExtent2D GetSwapChainExtent() const { return swapChainExtent; }
        const std::vector<VkImage>& GetSwapChainImages() const { return swapChainImages; }
        const std::vector<VkImageView>& GetSwapChainImageViews() const { return swapChainImageViews; }

        VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

        SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);

        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    private:
        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        uint32_t graphicsQueueFamily = 0;
        VkQueue presentQueue = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;

        VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

        // Swapchain
        VkSwapchainKHR swapChain = VK_NULL_HANDLE;
        std::vector<VkImage> swapChainImages;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        std::vector<VkImageView> swapChainImageViews;

    public:
        // Helper for one-time commands
        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    private:
        GLFWwindow* m_Window;

        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface();
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateAllocator();
        void CreateCommandPool();

        void CreateImageViews();
        VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

        bool CheckValidationLayerSupport();
        bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
        int RateDeviceSuitability(VkPhysicalDevice device);
        std::vector<const char*> GetRequiredExtensions();
        VkSampleCountFlagBits getMaxUsableSampleCount();
    };
}
