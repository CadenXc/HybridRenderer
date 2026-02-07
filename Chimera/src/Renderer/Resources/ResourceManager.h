#pragma once

#include "pch.h"
#include "Renderer/Backend/VulkanCommon.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Resources/Image.h"
#include "Renderer/Resources/Material.h"
#include "Renderer/Resources/ResourceHandle.h"
#include "Scene/SceneCommon.h"

namespace Chimera {

	struct UniformBufferObject
	{
		glm::mat4 view;
		glm::mat4 proj;
		glm::mat4 viewInverse;
		glm::mat4 projInverse;
		glm::mat4 viewProjInverse;
		glm::mat4 prevView;
		glm::mat4 prevProj;
		DirectionalLight directionalLight;
		glm::vec2 displaySize;
		glm::vec2 displaySizeInverse;
		uint32_t frameIndex;
		uint32_t frameCount;
        uint32_t displayMode;
        glm::vec4 cameraPos;
	};

	class ResourceManager
	{
	public:
		ResourceManager(std::shared_ptr<class VulkanContext> context);
		~ResourceManager();

		void InitGlobalResources();
		void UpdateGlobalResources(uint32_t currentFrame, const UniformBufferObject& ubo);

		VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }
		VkDescriptorPool GetTransientDescriptorPool() const { return m_TransientDescriptorPool; }
		void ResetTransientDescriptorPool();

		// Samplers
		VkSampler GetDefaultSampler() const { return m_TextureSampler; }
        Image* GetDefaultTexture() const { return m_Textures.empty() ? nullptr : m_Textures[0].get(); }

		// Graph Resource Creation
		GraphImage CreateGraphImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageLayout initialLayout, VkSampleCountFlagBits samples);
		void DestroyGraphImage(GraphImage& image);

        static ResourceManager* Get() { return s_Instance; }

		// Resource Access
		template<typename T> T* Get(Handle<T> handle);
        Image* GetTexture(TextureHandle handle);
        Material* GetMaterial(MaterialHandle handle);
        Buffer* GetBuffer(BufferHandle handle);

		// Loading
		TextureHandle LoadTexture(const std::string& path);
		TextureHandle LoadHDRTexture(const std::string& path);
		TextureHandle AddTexture(std::unique_ptr<Image> texture, const std::string& name = "");
        TextureHandle GetTextureIndex(const std::string& name);
		
        // Materials
        MaterialHandle CreateMaterial(const std::string& name = "");
        MaterialHandle AddMaterial(std::unique_ptr<Material> material, const std::string& name = "");
        
        // GPU Material Data
        VkBuffer GetMaterialBuffer() const { return m_MaterialBuffer->GetBuffer(); }
        void SyncMaterialsToGPU();

        // Reference Counting
        void AddRef(TextureHandle handle);
        void Release(TextureHandle handle);
        uint32_t GetRefCount(TextureHandle handle);

        void AddRef(BufferHandle handle);
        void Release(BufferHandle handle);
        uint32_t GetRefCount(BufferHandle handle);

        void AddRef(MaterialHandle handle);
        void Release(MaterialHandle handle);

		// Reference Walnut: Deferred resource deletion
		static void SubmitResourceFree(std::function<void()>&& func);
		void ClearResourceFreeQueue(uint32_t frameIndex);
        void UpdateFrameIndex(uint32_t frameIndex) { m_CurrentFrameIndex = frameIndex; }

        // Raw Access (use sparingly)
        const std::vector<std::unique_ptr<Image>>& GetTextures() const { return m_Textures; }
        const std::vector<std::unique_ptr<Material>>& GetMaterials() const { return m_Materials; }

	private:
		void CreateDescriptorPool();
		void CreateTransientDescriptorPool();
		void CreateTextureSampler();

	private:
		static ResourceManager* s_Instance;
		std::shared_ptr<class VulkanContext> m_Context;
		
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
		VkDescriptorPool m_TransientDescriptorPool = VK_NULL_HANDLE;
		
		std::vector<std::unique_ptr<Buffer>> m_UniformBuffers;
		
		std::vector<std::unique_ptr<Image>> m_Textures;
        VkSampler m_TextureSampler = VK_NULL_HANDLE;
        std::unordered_map<std::string, TextureHandle> m_TextureMap;
        std::vector<uint32_t> m_TextureRefCount;

        std::vector<std::unique_ptr<Material>> m_Materials;
        std::unordered_map<std::string, MaterialHandle> m_MaterialMap;
        std::vector<uint32_t> m_MaterialRefCount;
        std::unique_ptr<Buffer> m_MaterialBuffer;

        std::vector<std::unique_ptr<Buffer>> m_Buffers;
        std::vector<uint32_t> m_BufferRefCount;

		// One queue per frame in flight (using MaxFramesInFlight from Renderer)
		std::vector<std::vector<std::function<void()>>> m_ResourceFreeQueue;
        uint32_t m_CurrentFrameIndex = 0;
	};

    // Template implementations
    template<> inline Image* ResourceManager::Get(TextureHandle handle) { return GetTexture(handle); }
    template<> inline Material* ResourceManager::Get(MaterialHandle handle) { return GetMaterial(handle); }
    template<> inline Buffer* ResourceManager::Get(BufferHandle handle) { return GetBuffer(handle); }

}
