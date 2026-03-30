#pragma once
#include "pch.h"
#include "Swapchain.h"
#include "VulkanInstance.h"
#include "VulkanDevice.h"
#include "DeletionQueue.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <thread>

namespace Chimera
{
    class VulkanContext : public std::enable_shared_from_this<VulkanContext>
    {
    public:
        ~VulkanContext();

        static VulkanContext& Get();
        static void Destroy();
        
        std::shared_ptr<VulkanContext> GetShared()
        {
            return shared_from_this();
        }

        VkDevice GetDevice() const { return m_Device->GetHandle(); }
        VkPhysicalDevice GetPhysicalDevice() const { return m_Device->GetPhysicalDevice(); }
        VkInstance GetInstance() const { return m_Instance->GetHandle(); }
        VmaAllocator GetAllocator() const { return m_Device->GetAllocator(); }
        VkSurfaceKHR GetSurface() const { return m_Surface; }
        GLFWwindow* GetWindow() const { return m_Window; }

        VkQueue GetGraphicsQueue() const { return m_Device->GetGraphicsQueue(); }
        VkQueue GetComputeQueue() const { return m_Device->GetComputeQueue(); }
        VkQueue GetPresentQueue() const { return m_Device->GetPresentQueue(); }
        uint32_t GetGraphicsQueueFamily() const { return m_Device->GetGraphicsQueueFamily(); }
        uint32_t GetComputeQueueFamily() const { return m_Device->GetComputeQueueFamily(); }
        
        VkCommandPool GetCommandPool() const { return m_CommandPool; }
        VkCommandPool GetThreadLocalCommandPool();

        // [CRITICAL] Global lock for all Queue operations across all threads
        static std::mutex& GetGlobalQueueMutex();

        std::shared_ptr<Swapchain> GetSwapchain() const { return m_Swapchain; }
        VkSwapchainKHR GetSwapChain() const { return m_Swapchain->GetHandle(); }
        VkFormat GetSwapChainImageFormat() const { return m_Swapchain->GetFormat(); }
        VkExtent2D GetSwapChainExtent() const { return m_Swapchain->GetExtent(); }
        const std::vector<VkImage>& GetSwapChainImages() const { return m_Swapchain->GetImages(); }
        void RecreateSwapChain() { m_Swapchain->Recreate(); }

        VkDescriptorSetLayout GetEmptyDescriptorSetLayout() const { return m_EmptyDescriptorSetLayout; }
        VkDescriptorSet GetEmptyDescriptorSet() const { return m_EmptyDescriptorSet; }
        VkDescriptorSet& GetEmptyDescriptorSetRef() { return m_EmptyDescriptorSet; }

        bool IsRayTracingSupported() const { return m_Device->IsRayTracingSupported(); }
        const VkPhysicalDeviceProperties& GetDeviceProperties() const { return m_Device->GetProperties(); }
        const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& GetRayTracingProperties() const { return m_Device->GetRTProperties(); }
        VkSampleCountFlagBits GetMSAASamples() const { return m_Device->GetMaxUsableSampleCount(); }

        VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);
        void SetDebugName(uint64_t handle, VkObjectType type, const char* name);

        DeletionQueue& GetDeletionQueue() { return m_DeletionQueue; }

    private:
        VulkanContext();
        void CreateSurface();
        void CreateCommandPool();
        void CreateEmptyLayout();

    private:
        static VulkanContext* s_Instance;
        GLFWwindow* m_Window;
        std::unique_ptr<VulkanInstance> m_Instance;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        std::unique_ptr<VulkanDevice> m_Device;
        std::shared_ptr<Swapchain> m_Swapchain;

        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_EmptyDescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet m_EmptyDescriptorSet = VK_NULL_HANDLE;
        DeletionQueue m_DeletionQueue;
        
        std::mutex m_PoolMutex;
        std::unordered_map<std::thread::id, VkCommandPool> m_ThreadCommandPools;
    };
}
