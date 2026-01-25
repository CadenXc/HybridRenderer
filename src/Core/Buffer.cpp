#include "pch.h"
#include "Buffer.h"

namespace Chimera {

    Buffer::Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
        : m_Allocator(allocator), m_Size(size)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = memoryUsage;
        
        // 如果是 CPU 可写的内存，自动添加映射标志，方便后续 Map 调用
        if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU || memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY)
        {
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            m_PersistentlyMapped = true;
        }

        VmaAllocationInfo allocationResultInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &allocationResultInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create buffer!");
        }

        // 如果创建时已经映射了，直接保存指针
        if (allocationResultInfo.pMappedData) {
            m_MappedData = allocationResultInfo.pMappedData;
        }

        // 检查内存是否 Coherent（对 Flush 有影响）
        VmaAllocationInfo allocInfo2;
        vmaGetAllocationInfo(allocator, m_Allocation, &allocInfo2);
        m_IsCoherent = (allocInfo2.memoryType & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

        // 保存 Device 指针用于 Flush 操作
        VmaAllocatorInfo allocatorInfo = {};
        vmaGetAllocatorInfo(allocator, &allocatorInfo);
        m_Device = allocatorInfo.device;

        // 如果包含 Shader Device Address 用途，获取并缓存地址 (用于光线追踪)
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        {
            VkBufferDeviceAddressInfo deviceAddressInfo{};
            deviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            deviceAddressInfo.buffer = m_Buffer;
            
            m_DeviceAddress = vkGetBufferDeviceAddress(m_Device, &deviceAddressInfo);
        }
    }

    Buffer::~Buffer()
    {
        if (m_Buffer != VK_NULL_HANDLE && m_Allocator != VK_NULL_HANDLE)
        {
            // 如果我们手动 Map 过且不是持续映射的，应该 Unmap，但 VMA 会在 Destroy 时自动处理
            vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
        }
    }

    Buffer::Buffer(Buffer&& other) noexcept
        : m_Allocator(other.m_Allocator),
          m_Buffer(other.m_Buffer),
          m_Allocation(other.m_Allocation),
          m_Size(other.m_Size),
          m_DeviceAddress(other.m_DeviceAddress),
          m_MappedData(other.m_MappedData),
          m_PersistentlyMapped(other.m_PersistentlyMapped)
    {
        other.m_Buffer = VK_NULL_HANDLE;
        other.m_Allocation = VK_NULL_HANDLE;
        other.m_MappedData = nullptr;
        other.m_PersistentlyMapped = false;
    }

    Buffer& Buffer::operator=(Buffer&& other) noexcept
    {
        if (this != &other)
        {
            // 先释放当前资源
            if (m_Buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
            }

            m_Allocator = other.m_Allocator;
            m_Buffer = other.m_Buffer;
            m_Allocation = other.m_Allocation;
            m_Size = other.m_Size;
            m_DeviceAddress = other.m_DeviceAddress;
            m_MappedData = other.m_MappedData;
            m_PersistentlyMapped = other.m_PersistentlyMapped;

            other.m_Buffer = VK_NULL_HANDLE;
            other.m_Allocation = VK_NULL_HANDLE;
            other.m_MappedData = nullptr;
            other.m_PersistentlyMapped = false;
        }
        return *this;
    }

    void* Buffer::Map()
    {
        if (m_MappedData) return m_MappedData;

        if (vmaMapMemory(m_Allocator, m_Allocation, &m_MappedData) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to map buffer memory!");
        }
        return m_MappedData;
    }

    void Buffer::Unmap()
    {
        if (m_PersistentlyMapped) return;

        if (m_MappedData)
        {
            vmaUnmapMemory(m_Allocator, m_Allocation);
            m_MappedData = nullptr;
        }
    }

    void Buffer::UploadData(const void* data, size_t size)
    {
        void* dest = Map();
        memcpy(dest, data, size);
        // 对于非 Coherent 内存，需要 Flush 才能保证 GPU 能看到更新
        if (!m_IsCoherent) {
            Flush(0, size);
        }
    }

    void Buffer::Flush(VkDeviceSize offset, VkDeviceSize size)
    {
        if (m_IsCoherent) return;

        // 使用 VMA 的函数代替手动的 vkFlushMappedMemoryRanges
        // VMA 会自动处理 offset 和 alignment
        vmaFlushAllocation(m_Allocator, m_Allocation, offset, (size == VK_WHOLE_SIZE) ? m_Size : size);
    }
}