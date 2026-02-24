#pragma once

#include "pch.h"
#include "Renderer/ChimeraCommon.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Resources/Image.h"
#include "Renderer/Resources/Material.h"
#include "Renderer/Resources/ResourceHandle.h"
#include "Renderer/Graph/RenderGraphCommon.h"
#include "Scene/SceneCommon.h"

namespace Chimera
{
    class ResourceManager
    {
    public:
        ResourceManager();
        ~ResourceManager();

        void Clear(); // [NEW] Explicit cleanup
        void InitGlobalResources();
        void UpdateGlobalResources(uint32_t currentFrame, const UniformBufferObject& ubo);
        
        void UpdateSceneDescriptorSet(class Scene* scene, uint32_t frameIndex);
        void UpdateSceneDescriptorSet(class Scene* scene) { UpdateSceneDescriptorSet(scene, 0xFFFFFFFF); }

        VkDescriptorSet GetSceneDescriptorSet(uint32_t frameIndex) const { return m_SceneDescriptorSets[frameIndex]; }
        VkDescriptorSet GetSceneDescriptorSet() const { return m_SceneDescriptorSets[0]; }
        VkDescriptorSetLayout GetSceneDescriptorSetLayout() const { return m_SceneDescriptorSetLayout; }

        VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }
        VkDescriptorPool GetTransientDescriptorPool() const { return m_TransientDescriptorPools[m_CurrentFrameIndex]; }
        void ResetTransientDescriptorPool();

        VkSampler GetDefaultSampler() const { return m_TextureSampler; }
        Image* GetDefaultTexture() const { return m_Textures.empty() ? nullptr : m_Textures[0].get(); }
        Image& GetBlackTexture() const { return *m_Textures[0]; }

        GraphImage CreateGraphImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageLayout initialLayout, VkSampleCountFlagBits samples, const std::string& name = "");
        void DestroyGraphImage(GraphImage& image);

        static bool HasInstance() { return s_Instance != nullptr; }
        static ResourceManager& Get() { return *s_Instance; }

        template<typename T> T* Get(Handle<T> handle);
        Image* GetTexture(TextureHandle handle);
        Material* GetMaterial(MaterialHandle handle);
        Buffer* GetBuffer(BufferHandle handle);

        TextureHandle LoadTexture(const std::string& path, bool srgb = true);
        TextureHandle LoadHDRTexture(const std::string& path);
        TextureHandle AddTexture(std::unique_ptr<Image> texture, const std::string& name = "");
        TextureHandle GetTextureIndex(const std::string& name);
        
        MaterialHandle CreateMaterial(const std::string& name = "");
        MaterialHandle AddMaterial(std::unique_ptr<Material> material, const std::string& name = "");
        
        VkBuffer GetMaterialBuffer() const { return (VkBuffer)m_MaterialBuffer->GetBuffer(); }
        void SyncMaterialsToGPU();
        void SyncPrimitivesToGPU(class Scene* scene); // [NEW] Data-driven primitive sync

        void AddTransientBuffer(std::shared_ptr<Buffer> buffer)
        {
            m_TransientBuffers[m_CurrentFrameIndex].push_back(buffer);
        }

        void AddRef(TextureHandle handle);
        void Release(TextureHandle handle);
        uint32_t GetRefCount(TextureHandle handle);

        void AddRef(BufferHandle handle);
        void Release(BufferHandle handle);
        uint32_t GetRefCount(BufferHandle handle);

        void AddRef(MaterialHandle handle);
        void Release(MaterialHandle handle);

        static void SubmitResourceFree(std::function<void()>&& func);
        void ClearResourceFreeQueue(uint32_t frameIndex);
        
        void UpdateFrameIndex(uint32_t frameIndex) { m_CurrentFrameIndex = frameIndex; }

        const std::vector<std::unique_ptr<Image>>& GetTextures() const { return m_Textures; }
        const std::vector<std::unique_ptr<Material>>& GetMaterials() const { return m_Materials; }

        // --- Scene Management [NEW] ---
        void SetActiveScene(std::shared_ptr<class Scene> scene) { m_ActiveScene = scene; }
        class Scene* GetActiveScene() const { return m_ActiveScene.get(); }
        std::shared_ptr<class Scene> GetActiveSceneShared() const { return m_ActiveScene; }
        bool HasActiveScene() const { return m_ActiveScene != nullptr; }

    private:
        void CreateDescriptorPool();
        void CreateTransientDescriptorPools(); // [FIX] Plural
        void CreateTextureSampler();
        void CreateSceneDescriptorSetLayout();
        void AllocatePersistentSets();
        void CreateDefaultResources();

    private:
        static ResourceManager* s_Instance;
        
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> m_TransientDescriptorPools; // [FIX]
        
        VkDescriptorSetLayout m_SceneDescriptorSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_SceneDescriptorSets;

        std::vector<std::unique_ptr<Buffer>> m_UniformBuffers;
        std::vector<std::unique_ptr<Image>> m_Textures;
        VkSampler m_TextureSampler = VK_NULL_HANDLE;
        std::unordered_map<std::string, TextureHandle> m_TextureMap;
        std::vector<uint32_t> m_TextureRefCount;

        std::vector<std::unique_ptr<Material>> m_Materials;
        std::unordered_map<std::string, MaterialHandle> m_MaterialMap;
        std::vector<uint32_t> m_MaterialRefCount;
        std::unique_ptr<Buffer> m_MaterialBuffer;
        std::unique_ptr<Buffer> m_PrimitiveBuffer; // [NEW] SSBO for all scene objects

        std::vector<std::shared_ptr<Buffer>> m_Buffers;
        std::vector<uint32_t> m_BufferRefCount;
        std::vector<std::shared_ptr<Buffer>> m_TransientBuffers[MAX_FRAMES_IN_FLIGHT];

        std::vector<std::vector<std::function<void()>>> m_ResourceFreeQueue;
        std::shared_ptr<class Scene> m_ActiveScene; // [CENTRALIZED OWNERSHIP]
        uint32_t m_CurrentFrameIndex = 0;
        bool m_IsCleared = false; // [NEW] Safety flag
    };

    template<> inline Image* ResourceManager::Get(TextureHandle handle) { return GetTexture(handle); }
    template<> inline Material* ResourceManager::Get(MaterialHandle handle) { return GetMaterial(handle); }
    template<> inline Buffer* ResourceManager::Get(BufferHandle handle) { return GetBuffer(handle); }
}