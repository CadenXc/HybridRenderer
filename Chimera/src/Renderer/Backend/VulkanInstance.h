#pragma once
#include "pch.h"

namespace Chimera {

    class VulkanInstance {
    public:
        VulkanInstance(const std::string& appName);
        ~VulkanInstance();

        VkInstance GetHandle() const { return m_Instance; }

        static bool CheckValidationLayerSupport();
        static std::vector<const char*> GetRequiredExtensions();

    private:
        void CreateInstance(const std::string& appName);
        void SetupDebugMessenger();

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    };

}
