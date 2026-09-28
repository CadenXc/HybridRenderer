#include "pch.h"
#include "VulkanContext.h"
#include "Core/Application.h"

namespace Chimera
{
VulkanContext* VulkanContext::s_Instance = nullptr;
static std::shared_ptr<VulkanContext> s_SharedInstance = nullptr;
static std::mutex s_GlobalQueueMutex; // The ONE lock to rule them all

std::mutex& VulkanContext::GetGlobalQueueMutex()
{
    return s_GlobalQueueMutex;
}

VulkanContext& VulkanContext::Get()
{
    if (!s_SharedInstance)
    {
        s_SharedInstance = std::shared_ptr<VulkanContext>(new VulkanContext());
        s_Instance = s_SharedInstance.get();
    }
    return *s_SharedInstance;
}

void VulkanContext::Destroy()
{
    s_SharedInstance.reset();
    s_Instance = nullptr;
}

VulkanContext::VulkanContext()
{
    m_Window = Application::Get().GetWindow().GetNativeWindow();
    s_Instance = this;
    CH_CORE_INFO("VulkanContext: Creating core Vulkan link...");

    try
    {
        m_Instance = std::make_unique<VulkanInstance>("Chimera Engine");
        CreateSurface();
        m_Device =
            std::make_unique<VulkanDevice>(m_Instance->GetHandle(), m_Surface);

        CreateCommandPool();
        m_DeletionQueue.Init(3);
        m_Swapchain =
            std::make_shared<Swapchain>(GetDevice(), GetPhysicalDevice(),
                                        m_Surface, m_Window);

        CreateEmptyLayout();
    }
    catch (...)
    {
        Cleanup();
        s_Instance = nullptr;
        throw;
    }

    CH_CORE_INFO("VulkanContext Initialized.");
}

VulkanContext::~VulkanContext()
{
    CH_CORE_INFO("VulkanContext: Destructor CALLED.");
    s_Instance = nullptr;
    Cleanup();
    CH_CORE_INFO("VulkanContext: Device and Instance destroyed.");
}

void VulkanContext::Cleanup() noexcept
{
    if (m_Device)
    {
        vkDeviceWaitIdle(GetDevice());
    }

    // Flush callbacks only while their Vulkan device is still alive.
    if (m_Device) m_DeletionQueue.FlushAll();

    if (m_Swapchain)
    {
        m_Swapchain.reset();
    }

    if (m_Device && m_EmptyDescriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(GetDevice(), m_EmptyDescriptorSetLayout,
                                     nullptr);
        m_EmptyDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_Device && m_CommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(GetDevice(), m_CommandPool, nullptr);
        m_CommandPool = VK_NULL_HANDLE;
    }

    if (m_Device)
    {
        std::lock_guard<std::mutex> lock(m_PoolMutex);
        for (auto& [id, pool] : m_ThreadCommandPools)
        {
            vkDestroyCommandPool(GetDevice(), pool, nullptr);
        }
        m_ThreadCommandPools.clear();
    }

    if (m_Device)
        CH_CORE_INFO(
            "VulkanContext: Resetting Device (Triggering VMA destruction)...");
    m_Device.reset();

    if (m_Instance && m_Surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(m_Instance->GetHandle(), m_Surface, nullptr);
        m_Surface = VK_NULL_HANDLE;
    }

    m_Instance.reset();
}

void VulkanContext::CreateSurface()
{
    VK_CHECK(glfwCreateWindowSurface(m_Instance->GetHandle(), m_Window, nullptr,
                                     &m_Surface));
}

void VulkanContext::CreateCommandPool()
{
    QueueFamilyIndices indices =
        m_Device->FindQueueFamilies(GetPhysicalDevice(), m_Surface);
    VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

    VK_CHECK(
        vkCreateCommandPool(GetDevice(), &poolInfo, nullptr, &m_CommandPool));
    SetDebugName((uint64_t)m_CommandPool, VK_OBJECT_TYPE_COMMAND_POOL,
                 "System_CommandPool");
}

void VulkanContext::CreateEmptyLayout()
{
    VkDescriptorSetLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VK_CHECK(vkCreateDescriptorSetLayout(GetDevice(), &layoutInfo, nullptr,
                                         &m_EmptyDescriptorSetLayout));
    SetDebugName((uint64_t)m_EmptyDescriptorSetLayout,
                 VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "System_EmptyLayout");
}

VkImageView VulkanContext::CreateImageView(VkImage image, VkFormat format,
                                           VkImageAspectFlags aspectFlags,
                                           uint32_t mipLevels)
{
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(GetDevice(), &viewInfo, nullptr, &imageView) !=
        VK_SUCCESS)
    {
        throw std::runtime_error("failed to create image view!");
    }
    return imageView;
}

void VulkanContext::SetDebugName(uint64_t handle, VkObjectType type,
                                 const char* name)
{
    if (handle == 0 || name == nullptr)
    {
        return;
    }
    auto func = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(
        GetDevice(), "vkSetDebugUtilsObjectNameEXT");
    if (func)
    {
        VkDebugUtilsObjectNameInfoEXT nameInfo{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        nameInfo.objectType = type;
        nameInfo.objectHandle = handle;
        nameInfo.pObjectName = name;
        func(GetDevice(), &nameInfo);
    }
}

VkCommandPool VulkanContext::GetThreadLocalCommandPool()
{
    std::thread::id tid = std::this_thread::get_id();

    {
        std::lock_guard<std::mutex> lock(m_PoolMutex);
        if (m_ThreadCommandPools.count(tid))
        {
            return m_ThreadCommandPools[tid];
        }
    }

        // Create a new pool for this thread
    VkCommandPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                     VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_Device->GetGraphicsQueueFamily();

    VkCommandPool pool;
    VK_CHECK(vkCreateCommandPool(GetDevice(), &poolInfo, nullptr, &pool));

    {
        std::lock_guard<std::mutex> lock(m_PoolMutex);
        m_ThreadCommandPools[tid] = pool;
    }

    CH_CORE_INFO(
        "VulkanContext: Created new thread-local CommandPool for Thread {0}",
        (uint64_t)*(uint64_t*)&tid);
    return pool;
}
} // namespace Chimera
