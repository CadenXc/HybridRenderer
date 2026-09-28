#include "pch.h"
#include "Renderer/Resources/Buffer.h"
#include "Core/Application.h"
#include "Renderer/Backend/VulkanContext.h"

namespace Chimera
{

    Buffer::Buffer(VkDeviceSize size,
               VkBufferUsageFlags usage,
               VmaMemoryUsage memoryUsage,
               const std::string& name,
               VkDeviceSize minAlignment)
    : m_Size(size)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU ||
        memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY)
    {
        allocInfo.flags =
            VMA_ALLOCATION_CREATE_MAPPED_BIT |
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        m_PersistentlyMapped = true;
    }

    VmaAllocationInfo allocationResultInfo{};
    VkResult result = VK_SUCCESS;
    m_Allocator = VulkanContext::Get().GetAllocator();

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        const VkDeviceSize allocationAlignment =
            std::max<VkDeviceSize>(256, minAlignment);

        result = vmaCreateBufferWithAlignment(
            m_Allocator, &bufferInfo, &allocInfo, allocationAlignment, &m_Buffer, &m_Allocation,
            &allocationResultInfo);
    }
    else
    {
        result =
            vmaCreateBuffer(m_Allocator, &bufferInfo, &allocInfo, &m_Buffer,
                            &m_Allocation, &allocationResultInfo);
    }

    if (result != VK_SUCCESS)
    {
        CH_CORE_ERROR("Buffer: Failed to create buffer! Result: {0}, Size: {1}, Name: {2}", (int)result, size, name);
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryPropertyFlags memoryProperties = 0;
    vmaGetAllocationMemoryProperties(m_Allocator, m_Allocation,
                                     &memoryProperties);

    m_IsCoherent =
        (memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    CH_CORE_TRACE("Buffer: ALLOCATED. Handle: [0x{:x}], Size: {}, Name: {}",
                  (uint64_t)m_Buffer, m_Size, name);

    if (!name.empty())
    {
        VulkanContext::Get().SetDebugName((uint64_t)m_Buffer,
                                          VK_OBJECT_TYPE_BUFFER, name.c_str());
    }

    if (allocationResultInfo.pMappedData)
    {
        m_MappedData = allocationResultInfo.pMappedData;
    }

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        VkBufferDeviceAddressInfo deviceAddressInfo{};
        deviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        deviceAddressInfo.buffer = m_Buffer;
        m_DeviceAddress = vkGetBufferDeviceAddress(
            VulkanContext::Get().GetDevice(), &deviceAddressInfo);
        CH_CORE_TRACE("Buffer: Device Address [0x{:x}]", m_DeviceAddress);
    }
}

Buffer::~Buffer()
{
    Release();
}

void Buffer::Release()
{
    if (m_Buffer != VK_NULL_HANDLE && m_Allocator != nullptr)
    {
        CH_CORE_TRACE("Buffer: DESTROYING. Handle: [0x{:x}], Size: {}",
                      (uint64_t)m_Buffer, m_Size);
        if (m_DeviceAddress != 0)
        {
            CH_CORE_TRACE("Buffer: Releasing Device Address [0x{:x}]",
                          m_DeviceAddress);
        }

        if (m_MappedData != nullptr && !m_PersistentlyMapped)
        {
            vmaUnmapMemory(m_Allocator, m_Allocation);
        }

        vmaDestroyBuffer(m_Allocator, m_Buffer, m_Allocation);
    }

    m_Buffer = VK_NULL_HANDLE;
    m_Allocation = VK_NULL_HANDLE;
    m_MappedData = nullptr;
    m_DeviceAddress = 0;
    m_Size = 0;
    m_PersistentlyMapped = false;
    m_IsCoherent = false;
    m_Allocator = nullptr;
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_Allocator(other.m_Allocator),
      m_Buffer(other.m_Buffer),
      m_Allocation(other.m_Allocation),
      m_Size(other.m_Size),
      m_DeviceAddress(other.m_DeviceAddress),
      m_MappedData(other.m_MappedData),
      m_PersistentlyMapped(other.m_PersistentlyMapped),
	  m_IsCoherent(other.m_IsCoherent)
{
    other.m_Allocator = nullptr;
    other.m_Buffer = VK_NULL_HANDLE;
    other.m_Allocation = VK_NULL_HANDLE;
    other.m_MappedData = nullptr;
    other.m_PersistentlyMapped = false;
    other.m_IsCoherent = false;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other)
    {
        Release();

        m_Allocator = other.m_Allocator;
        m_Buffer = other.m_Buffer;
        m_Allocation = other.m_Allocation;
        m_Size = other.m_Size;
        m_DeviceAddress = other.m_DeviceAddress;
        m_MappedData = other.m_MappedData;
        m_PersistentlyMapped = other.m_PersistentlyMapped;
        m_IsCoherent = other.m_IsCoherent;

        other.m_Allocator = nullptr;
        other.m_Buffer = VK_NULL_HANDLE;
        other.m_Allocation = VK_NULL_HANDLE;
        other.m_MappedData = nullptr;
        other.m_PersistentlyMapped = false;
        other.m_IsCoherent = false;
    }
    return *this;
}

void* Buffer::Map()
{
    if (m_MappedData)
    {
        return m_MappedData;
    }
    VkResult result =
        vmaMapMemory(m_Allocator, m_Allocation, &m_MappedData);
    if (result != VK_SUCCESS)
    {
        m_MappedData = nullptr;
        CH_CORE_ERROR("Buffer: vmaMapMemory failed with VkResult: {}",
                      static_cast<int>(result));
    }
    return m_MappedData;
}

void Buffer::Unmap()
{
    if (m_PersistentlyMapped || m_MappedData == nullptr)
    {
        return;
    }
    vmaUnmapMemory(m_Allocator, m_Allocation);
    m_MappedData = nullptr;
}

void Buffer::Update(const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    ValidateRange(size, offset, "Buffer::Update");
    if (size == 0)
    {
        return;
    }
    if (data == nullptr)
    {
        throw std::invalid_argument(
            "Buffer::Update requires non-null data for a non-empty write");
    }

    void* mapped = Map();
    if (mapped == nullptr)
    {
        throw std::runtime_error("Buffer::Update failed to map the buffer");
    }
    memcpy((uint8_t*)mapped + offset, data, size);

    if (!m_IsCoherent)
    {
        Flush(size, offset);
    }
}

void Buffer::Flush(VkDeviceSize size, VkDeviceSize offset)
{
    ValidateRange(size, offset, "Buffer::Flush");
    if (m_IsCoherent)
    {
        return;
    }
    VK_CHECK(vmaFlushAllocation(m_Allocator, m_Allocation, offset, size));
}

void Buffer::Invalidate(VkDeviceSize size, VkDeviceSize offset)
{
    ValidateRange(size, offset, "Buffer::Invalidate");
    if (m_IsCoherent)
    {
        return;
    }

    VK_CHECK(vmaInvalidateAllocation(m_Allocator, m_Allocation, offset, size));
}

void Buffer::ValidateRange(VkDeviceSize size, VkDeviceSize offset,
                           const char* operation) const
{
    const bool offsetOutOfRange = offset > m_Size;
    const bool sizeOutOfRange =
        size != VK_WHOLE_SIZE &&
        (offsetOutOfRange || size > m_Size - offset);
    if (offsetOutOfRange || sizeOutOfRange)
    {
        throw std::out_of_range(std::string(operation) +
                                " range exceeds buffer size");
    }
}
} // namespace Chimera
