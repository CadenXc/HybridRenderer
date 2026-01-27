#include "pch.h"
#include "gfx/resources/Buffer.h"

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

        if (allocationResultInfo.pMappedData) {
            m_MappedData = allocationResultInfo.pMappedData;
        }

        // Check if memory is coherent
        VmaAllocationInfo allocInfo2;
        vmaGetAllocationInfo(allocator, m_Allocation, &allocInfo2);
        
        // Use VmaAllocator to get device properties if needed, or assume based on memory type
        // For simplicity, we just use the mapped data if available
        
        VmaAllocatorInfo allocatorInfo = {};
        vmaGetAllocatorInfo(allocator, &allocatorInfo);
        m_Device = allocatorInfo.device;

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
        vmaMapMemory(m_Allocator, m_Allocation, &m_MappedData);
        return m_MappedData;
    }

    void Buffer::Unmap()
    {
        if (m_PersistentlyMapped) return;
        vmaUnmapMemory(m_Allocator, m_Allocation);
        m_MappedData = nullptr;
    }

    void Buffer::Update(const void* data, VkDeviceSize size, VkDeviceSize offset)
    {
        void* mapped = Map();
        memcpy((uint8_t*)mapped + offset, data, size);
        
        if (!m_IsCoherent) {
            Flush(size, offset);
        }
    }

    void Buffer::Flush(VkDeviceSize size, VkDeviceSize offset)
    {
        if (m_IsCoherent) return;
        vmaFlushAllocation(m_Allocator, m_Allocation, offset, size);
    }

}
