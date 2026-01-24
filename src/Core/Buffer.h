#pragma once

#include "pch.h"
#include "vk_mem_alloc.h"

namespace Chimera {

    class Buffer {
    public:
        Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
        ~Buffer();

        // 禁止拷贝，防止重复释放显存
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        // 允许移动 (Move)，方便在 vector 中存放
        Buffer(Buffer&& other) noexcept;
        Buffer& operator=(Buffer&& other) noexcept;

        VkBuffer GetBuffer() const { return m_Buffer; }
        VkDeviceSize GetSize() const { return m_Size; }
        uint64_t GetDeviceAddress() const { return m_DeviceAddress; }

        // 映射显存 (用于 CPU 写入数据)
        void* Map();
        void Unmap();
        
        // 便捷函数：直接写入数据
        void UploadData(const void* data, size_t size);

    private:
        VmaAllocator m_Allocator;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkDeviceSize m_Size = 0;
        uint64_t m_DeviceAddress = 0; // 缓存光追所需的设备地址
        void* m_MappedData = nullptr; // 缓存映射后的指针
        bool m_PersistentlyMapped = false; // 是否是持续映射的内存
    };

}
