#pragma once

#include "pch.h"
#include "vk_mem_alloc.h"

namespace Chimera {

	class Buffer
	{
	public:
		Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
		~Buffer();

		// No copy
		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;

		// Move allowed
		Buffer(Buffer&& other) noexcept;
		Buffer& operator=(Buffer&& other) noexcept;

		VkBuffer GetBuffer() const { return m_Buffer; }
		VkDeviceSize GetSize() const { return m_Size; }
		uint64_t GetDeviceAddress() const { return m_DeviceAddress; }
		VmaAllocation GetAllocation() const { return m_Allocation; }

		void* Map();
		void Unmap();
		
		// Both names for compatibility
		void Update(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
		void UploadData(const void* data, VkDeviceSize size) { Update(data, size, 0); }
		
		void Flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

	private:
		VmaAllocator m_Allocator = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
		VkBuffer m_Buffer = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = VK_NULL_HANDLE;
		VkDeviceSize m_Size = 0;
		uint64_t m_DeviceAddress = 0;
		void* m_MappedData = nullptr;
		bool m_PersistentlyMapped = false;
		bool m_IsCoherent = false;
	};

}