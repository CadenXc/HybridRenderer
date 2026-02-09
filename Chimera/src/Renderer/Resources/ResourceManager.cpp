#include "pch.h"
#include "ResourceManager.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Resources/Image.h"
#include "Renderer/Resources/Material.h"
#include "Utils/VulkanBarrier.h"
#include "Core/Application.h"
#include "Renderer/RenderState.h"
#include "Scene/Scene.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Chimera
{
    // [Internal Helpers] Force complete types for handles locally
    struct VkBuffer_T {};
    struct VkImage_T {};
    #define BUF(x) ((VkBuffer)(void*)(uintptr_t)(x))
    #define IMG(x) ((VkImage)(void*)(uintptr_t)(x))

    ResourceManager* ResourceManager::s_Instance = nullptr;

    ResourceManager::ResourceManager()
    {
        s_Instance = this;
        m_ResourceFreeQueue.resize(MAX_FRAMES_IN_FLIGHT);
    }

    ResourceManager::~ResourceManager()
    {
        vkDeviceWaitIdle(VulkanContext::Get().GetDevice());
        for (auto& queue : m_ResourceFreeQueue)
        {
            for (auto& func : queue)
            {
                func();
            }
            queue.clear();
        }
        if (m_SceneDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(VulkanContext::Get().GetDevice(), m_SceneDescriptorSetLayout, nullptr);
        }
        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(VulkanContext::Get().GetDevice(), m_DescriptorPool, nullptr);
        }
        if (m_TransientDescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(VulkanContext::Get().GetDevice(), m_TransientDescriptorPool, nullptr);
        }
        if (m_TextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(VulkanContext::Get().GetDevice(), m_TextureSampler, nullptr);
        }
    }

    void ResourceManager::InitGlobalResources()
    {
        CH_CORE_INFO("ResourceManager: Initializing Global Resources...");
        CreateDescriptorPool();
        CreateTransientDescriptorPool();
        CreateTextureSampler();

        CreateSceneDescriptorSetLayout();
        AllocatePersistentSets();
        CreateDefaultResources();
    }

    void ResourceManager::CreateSceneDescriptorSetLayout()
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_ALL, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr },
            { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024, VK_SHADER_STAGE_ALL, nullptr }
        };

        VkDescriptorBindingFlags bindingsFlags[4] = { 
            0, 0, 0,
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT 
        };
        
        VkDescriptorSetLayoutBindingFlagsCreateInfo layoutFlags{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
        layoutFlags.bindingCount = 4;
        layoutFlags.pBindingFlags = bindingsFlags;

        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.pNext = &layoutFlags;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = (uint32_t)bindings.size();
        layoutInfo.pBindings = bindings.data();

        vkCreateDescriptorSetLayout(VulkanContext::Get().GetDevice(), &layoutInfo, nullptr, &m_SceneDescriptorSetLayout);
    }

    void ResourceManager::AllocatePersistentSets()
    {
        VkDescriptorSetLayout emptyLayout = VulkanContext::Get().GetEmptyDescriptorSetLayout();
        VkDescriptorSetAllocateInfo emptyAlloc{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        emptyAlloc.descriptorPool = m_DescriptorPool;
        emptyAlloc.descriptorSetCount = 1;
        emptyAlloc.pSetLayouts = &emptyLayout;
        vkAllocateDescriptorSets(VulkanContext::Get().GetDevice(), &emptyAlloc, &VulkanContext::Get().GetEmptyDescriptorSetRef());

        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_SceneDescriptorSetLayout;
        vkAllocateDescriptorSets(VulkanContext::Get().GetDevice(), &allocInfo, &m_SceneDescriptorSet);
    }

    void ResourceManager::CreateDefaultResources()
    {
        auto fallback = std::make_unique<Image>(1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        uint8_t magenta[] = { 255, 0, 255, 255 };
        Buffer staging(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        staging.Update(magenta, 4);
        {
            ScopedCommandBuffer cmd;
            VulkanUtils::TransitionImageLayout(cmd, IMG(fallback->GetImage()), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { 1, 1, 1 } };
            vkCmdCopyBufferToImage(cmd, BUF(staging.GetBuffer()), IMG(fallback->GetImage()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            VulkanUtils::TransitionImageLayout(cmd, IMG(fallback->GetImage()), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }
        AddTexture(std::move(fallback), "Default");
        CreateMaterial("Default");
        SyncMaterialsToGPU();
    }

    void ResourceManager::UpdateGlobalResources(uint32_t currentFrame, const UniformBufferObject& ubo)
    {
        m_CurrentFrameIndex = currentFrame;
    }

    void ResourceManager::UpdateSceneDescriptorSet(Scene* scene)
    {
        if (!scene || m_SceneDescriptorSet == VK_NULL_HANDLE)
        {
            return;
        }

        std::vector<VkWriteDescriptorSet> writes;
        
        // 0. AS
        VkAccelerationStructureKHR tlas = scene->GetTLAS();
        VkWriteDescriptorSetAccelerationStructureKHR asWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
        if (tlas != VK_NULL_HANDLE)
        {
            asWrite.accelerationStructureCount = 1;
            asWrite.pAccelerationStructures = &tlas;
            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = m_SceneDescriptorSet;
            w.dstBinding = 0;
            w.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            w.descriptorCount = 1;
            w.pNext = &asWrite;
            writes.push_back(w);
        }

        // 1. Materials
        if (m_MaterialBuffer)
        {
            VkDescriptorBufferInfo matInfo{ BUF(m_MaterialBuffer->GetBuffer()), 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet matW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            matW.dstSet = m_SceneDescriptorSet;
            matW.dstBinding = 1;
            matW.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            matW.descriptorCount = 1;
            matW.pBufferInfo = &matInfo;
            writes.push_back(matW);
        }

        // 2. Instances
        auto instBuf = scene->GetInstanceDataBuffer();
        if (instBuf)
        {
            VkDescriptorBufferInfo instInfo{ BUF(instBuf), 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet instW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            instW.dstSet = m_SceneDescriptorSet;
            instW.dstBinding = 2;
            instW.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            instW.descriptorCount = 1;
            instW.pBufferInfo = &instInfo;
            writes.push_back(instW);
        }

        // 3. Textures
        std::vector<VkDescriptorImageInfo> imageInfos;
        for (const auto& tex : m_Textures)
        {
            imageInfos.push_back({ m_TextureSampler, tex->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        }
        if (!imageInfos.empty())
        {
            VkWriteDescriptorSet texW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            texW.dstSet = m_SceneDescriptorSet;
            texW.dstBinding = 3;
            texW.dstArrayElement = 0;
            texW.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            texW.descriptorCount = (uint32_t)std::min((size_t)1024, imageInfos.size());
            texW.pImageInfo = imageInfos.data();
            writes.push_back(texW);
        }

        if (!writes.empty())
        {
            vkUpdateDescriptorSets(VulkanContext::Get().GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
        }
    }

    void ResourceManager::SyncMaterialsToGPU()
    {
        if (m_Materials.empty())
        {
            return;
        }
        VkDeviceSize bufferSize = std::max((VkDeviceSize)1024, sizeof(PBRMaterial) * m_Materials.size());
        if (!m_MaterialBuffer || m_MaterialBuffer->GetSize() < bufferSize)
        {
            m_MaterialBuffer = std::make_unique<Buffer>(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        }
        std::vector<PBRMaterial> materialData;
        for (const auto& mat : m_Materials)
        {
            materialData.push_back(mat ? mat->GetData() : PBRMaterial{});
        }
        Buffer staging(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        staging.UploadData(materialData.data(), sizeof(PBRMaterial) * materialData.size());
        {
            ScopedCommandBuffer cmd;
            VkBufferCopy copy{ 0, 0, sizeof(PBRMaterial) * materialData.size() };
            vkCmdCopyBuffer(cmd, BUF(staging.GetBuffer()), BUF(m_MaterialBuffer->GetBuffer()), 1, &copy);
        }
    }

    GraphImage ResourceManager::CreateGraphImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageLayout initialLayout, VkSampleCountFlagBits samples)
    {
        GraphImage img{};
        img.width = width;
        img.height = height;
        img.format = format;
        img.usage = usage;
        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = samples;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo vmaAllocInfo{};
        vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        vmaCreateImage(VulkanContext::Get().GetAllocator(), &imageInfo, &vmaAllocInfo, &img.handle, &img.allocation, nullptr);
        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = img.handle;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VulkanUtils::IsDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(VulkanContext::Get().GetDevice(), &viewInfo, nullptr, &img.view);
        if (VulkanUtils::IsDepthFormat(format))
        {
            viewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE };
            vkCreateImageView(VulkanContext::Get().GetDevice(), &viewInfo, nullptr, &img.debug_view);
        }
        else
        {
            img.debug_view = img.view;
        }
        return img;
    }

    void ResourceManager::DestroyGraphImage(GraphImage& image)
    {
        if (image.handle == VK_NULL_HANDLE)
        {
            return;
        }
        vkDestroyImageView(VulkanContext::Get().GetDevice(), image.view, nullptr);
        if (image.debug_view != image.view)
        {
            vkDestroyImageView(VulkanContext::Get().GetDevice(), image.debug_view, nullptr);
        }
        vmaDestroyImage(VulkanContext::Get().GetAllocator(), image.handle, image.allocation);
        image.handle = VK_NULL_HANDLE;
    }

    TextureHandle ResourceManager::LoadTexture(const std::string& path)
    {
        if (m_TextureMap.count(path))
        {
            return m_TextureMap[path];
        }
        int tw, th, tc;
        stbi_uc* pixels = stbi_load(path.c_str(), &tw, &th, &tc, STBI_rgb_alpha);
        if (!pixels)
        {
            return TextureHandle();
        }
        VkDeviceSize size = tw * th * 4;
        Buffer staging(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        staging.Update(pixels, size);
        stbi_image_free(pixels);
        auto image = std::make_unique<Image>((uint32_t)tw, (uint32_t)th, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        {
            ScopedCommandBuffer cmd;
            VulkanUtils::TransitionImageLayout(cmd, IMG(image->GetImage()), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)tw, (uint32_t)th, 1 } };
            vkCmdCopyBufferToImage(cmd, BUF(staging.GetBuffer()), IMG(image->GetImage()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            VulkanUtils::TransitionImageLayout(cmd, IMG(image->GetImage()), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }
        return AddTexture(std::move(image), path);
    }

    TextureHandle ResourceManager::LoadHDRTexture(const std::string& path)
    {
        if (m_TextureMap.count(path))
        {
            return m_TextureMap[path];
        }
        int tw, th, tc;
        float* pixels = stbi_loadf(path.c_str(), &tw, &th, &tc, STBI_rgb_alpha);
        if (!pixels)
        {
            return TextureHandle();
        }
        VkDeviceSize size = tw * th * 4 * sizeof(float);
        Buffer staging(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        staging.Update(pixels, size);
        stbi_image_free(pixels);
        auto image = std::make_unique<Image>((uint32_t)tw, (uint32_t)th, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        {
            ScopedCommandBuffer cmd;
            VulkanUtils::TransitionImageLayout(cmd, IMG(image->GetImage()), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)tw, (uint32_t)th, 1 } };
            vkCmdCopyBufferToImage(cmd, BUF(staging.GetBuffer()), IMG(image->GetImage()), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            VulkanUtils::TransitionImageLayout(cmd, IMG(image->GetImage()), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }
        return AddTexture(std::move(image), path);
    }

    TextureHandle ResourceManager::AddTexture(std::unique_ptr<Image> tex, const std::string& name)
    {
        uint32_t idx = (uint32_t)m_Textures.size();
        m_Textures.push_back(std::move(tex));
        m_TextureRefCount.push_back(1);
        if (!name.empty())
        {
            m_TextureMap[name] = TextureHandle(idx);
        }
        return TextureHandle(idx);
    }

    MaterialHandle ResourceManager::CreateMaterial(const std::string& n)
    {
        return AddMaterial(std::make_unique<Material>(n), n);
    }

    MaterialHandle ResourceManager::AddMaterial(std::unique_ptr<Material> m, const std::string& n)
    {
        uint32_t idx = (uint32_t)m_Materials.size();
        m_Materials.push_back(std::move(m));
        m_MaterialRefCount.push_back(1);
        if (!n.empty())
        {
            m_MaterialMap[n] = MaterialHandle(idx);
        }
        return MaterialHandle(idx);
    }

    void ResourceManager::AddRef(TextureHandle h)
    {
        if (h.id < m_TextureRefCount.size())
        {
            m_TextureRefCount[h.id]++;
        }
    }

    void ResourceManager::Release(TextureHandle h)
    {
        if (h.id < m_TextureRefCount.size() && --m_TextureRefCount[h.id] == 0 && h.id != 0)
        {
            Image* r = m_Textures[h.id].release();
            SubmitResourceFree([r](){ delete r; });
        }
    }

    uint32_t ResourceManager::GetRefCount(TextureHandle h)
    {
        return h.id < m_TextureRefCount.size() ? m_TextureRefCount[h.id] : 0;
    }

    void ResourceManager::AddRef(BufferHandle h)
    {
        if (h.id < m_BufferRefCount.size())
        {
            m_BufferRefCount[h.id]++;
        }
    }

    void ResourceManager::Release(BufferHandle h)
    {
        if (h.id < m_BufferRefCount.size() && --m_BufferRefCount[h.id] == 0)
        {
            Buffer* r = m_Buffers[h.id].release();
            SubmitResourceFree([r](){ delete r; });
        }
    }

    void ResourceManager::AddRef(MaterialHandle h)
    {
        if (h.id < m_MaterialRefCount.size())
        {
            m_MaterialRefCount[h.id]++;
        }
    }

    void ResourceManager::Release(MaterialHandle h)
    {
        if (h.id < m_MaterialRefCount.size() && --m_MaterialRefCount[h.id] == 0 && h.id != 0)
        {
            Material* r = m_Materials[h.id].release();
            SubmitResourceFree([r](){ delete r; });
        }
    }

    void ResourceManager::SubmitResourceFree(std::function<void()>&& f)
    {
        if (s_Instance)
        {
            s_Instance->m_ResourceFreeQueue[s_Instance->m_CurrentFrameIndex].push_back(std::move(f));
        }
    }

    void ResourceManager::ClearResourceFreeQueue(uint32_t fIdx)
    {
        for (auto& f : m_ResourceFreeQueue[fIdx])
        {
            f();
        }
        m_ResourceFreeQueue[fIdx].clear();
    }

    void ResourceManager::CreateDescriptorPool()
    {
        std::vector<VkDescriptorPoolSize> p = { {VK_DESCRIPTOR_TYPE_SAMPLER, 1000}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100} };
        VkDescriptorPoolCreateInfo i{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT, 1000, (uint32_t)p.size(), p.data()};
        vkCreateDescriptorPool(VulkanContext::Get().GetDevice(), &i, nullptr, &m_DescriptorPool);
    }

    void ResourceManager::CreateTransientDescriptorPool()
    {
        std::vector<VkDescriptorPoolSize> p = { {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4000}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4000}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4000}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4000}, {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100} };
        VkDescriptorPoolCreateInfo i{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 4000, (uint32_t)p.size(), p.data()};
        vkCreateDescriptorPool(VulkanContext::Get().GetDevice(), &i, nullptr, &m_TransientDescriptorPool);
    }

    void ResourceManager::CreateTextureSampler()
    {
        VkSamplerCreateInfo i{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        i.magFilter = VK_FILTER_LINEAR;
        i.minFilter = VK_FILTER_LINEAR;
        i.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        i.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        i.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        i.anisotropyEnable = VK_TRUE;
        i.maxAnisotropy = VulkanContext::Get().GetDeviceProperties().limits.maxSamplerAnisotropy;
        vkCreateSampler(VulkanContext::Get().GetDevice(), &i, nullptr, &m_TextureSampler);
    }

    Image* ResourceManager::GetTexture(TextureHandle h) { return (h.id < m_Textures.size() && m_Textures[h.id]) ? m_Textures[h.id].get() : m_Textures[0].get(); }
    Material* ResourceManager::GetMaterial(MaterialHandle h) { return (h.id < m_Materials.size() && m_Materials[h.id]) ? m_Materials[h.id].get() : m_Materials[0].get(); }
    Buffer* ResourceManager::GetBuffer(BufferHandle h) { return (h.id < m_Buffers.size() && m_Buffers[h.id]) ? m_Buffers[h.id].get() : nullptr; }
    TextureHandle ResourceManager::GetTextureIndex(const std::string& n) { return m_TextureMap.count(n) ? m_TextureMap[n] : TextureHandle(); }

    void AddRefInternal(Handle<Image> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().AddRef(h); }
    void ReleaseInternal(Handle<Image> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().Release(h); }
    void AddRefInternal(Handle<Buffer> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().AddRef(h); }
    void ReleaseInternal(Handle<Buffer> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().Release(h); }
    void AddRefInternal(Handle<Material> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().AddRef(h); }
    void ReleaseInternal(Handle<Material> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().Release(h); }
}
