#pragma once
#include "pch.h"

namespace Chimera
{
    struct SwapChainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    class Swapchain
    {
    public:
        Swapchain(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, GLFWwindow* window);
        ~Swapchain();

        void Recreate();

        VkSwapchainKHR GetHandle() const
        {
            return m_SwapChain;
        }

        VkFormat GetFormat() const
        {
            return m_SwapChainImageFormat;
        }

        VkExtent2D GetExtent() const
        {
            return m_SwapChainExtent;
        }

        uint32_t GetImageCount() const
        {
            return static_cast<uint32_t>(m_SwapChainImages.size());
        }

        const std::vector<VkImage>& GetImages() const
        {
            return m_SwapChainImages;
        }

        const std::vector<VkImageView>& GetImageViews() const
        {
            return m_SwapChainImageViews;
        }

        static SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

    private:
        void Create();
        void Cleanup();
        void CreateImageViews();

        VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    private:
        VkDevice m_Device;
        VkPhysicalDevice m_PhysicalDevice;
        VkSurfaceKHR m_Surface;
        GLFWwindow* m_Window;

        VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
        std::vector<VkImage> m_SwapChainImages;
        VkFormat m_SwapChainImageFormat;
        VkExtent2D m_SwapChainExtent;
        std::vector<VkImageView> m_SwapChainImageViews;
    };
}