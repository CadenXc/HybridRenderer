#include "pch.h"
#include "ResourceManager.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Backend/RenderContext.h"
#include "Renderer/Resources/Buffer.h"
#include "Renderer/Resources/Image.h"
#include "Renderer/Resources/Material.h"
#include "Utils/VulkanBarrier.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Chimera {

    ResourceManager* ResourceManager::s_Instance = nullptr;

    ResourceManager::ResourceManager(std::shared_ptr<VulkanContext> context)
        : m_Context(context)
    {
        s_Instance = this;
        m_ResourceFreeQueue.resize(MAX_FRAMES_IN_FLIGHT);
    }

    ResourceManager::~ResourceManager()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        
        for (auto& queue : m_ResourceFreeQueue) {
            for (auto& func : queue) func();
            queue.clear();
        }

        if (m_SceneDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_Context->GetDevice(), m_SceneDescriptorSetLayout, nullptr);
        if (m_DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_Context->GetDevice(), m_DescriptorPool, nullptr);
        if (m_TransientDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_Context->GetDevice(), m_TransientDescriptorPool, nullptr);
        if (m_TextureSampler != VK_NULL_HANDLE) vkDestroySampler(m_Context->GetDevice(), m_TextureSampler, nullptr);
    }

    void ResourceManager::UpdateSceneDescriptorSet()
    {
        if (!m_MaterialBuffer) return;

        std::vector<VkWriteDescriptorSet> writes;
        
        // 1. Material Buffer
        VkDescriptorBufferInfo bufferInfo{ m_MaterialBuffer->GetBuffer(), 0, m_MaterialBuffer->GetSize() };
        VkWriteDescriptorSet bufferWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        bufferWrite.dstSet = m_SceneDescriptorSet;
        bufferWrite.dstBinding = 0;
        bufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite.descriptorCount = 1;
        bufferWrite.pBufferInfo = &bufferInfo;
        writes.push_back(bufferWrite);

        // 2. Texture Array
        std::vector<VkDescriptorImageInfo> imageInfos;
        for (const auto& tex : m_Textures) {
            VkDescriptorImageInfo info{};
            info.sampler = m_TextureSampler;
            info.imageView = tex->GetImageView();
            info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos.push_back(info);
        }

        if (!imageInfos.empty()) {
            VkWriteDescriptorSet imageWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            imageWrite.dstSet = m_SceneDescriptorSet;
            imageWrite.dstBinding = 1;
            imageWrite.dstArrayElement = 0;
            imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            imageWrite.descriptorCount = (uint32_t)imageInfos.size();
            imageWrite.pImageInfo = imageInfos.data();
            writes.push_back(imageWrite);
        }

        vkUpdateDescriptorSets(m_Context->GetDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }

    void ResourceManager::InitGlobalResources()
    {
        CH_CORE_INFO("ResourceManager: Initializing Global Resources...");
        CreateDescriptorPool();
        CreateTransientDescriptorPool();
        CreateTextureSampler();

        // 1. Create Scene Descriptor Set Layout (Set 1)
        // Binding 0: Material Storage Buffer
        // Binding 1: Bindless Texture Array
        std::vector<VkDescriptorSetLayoutBinding> bindings = {
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr },
            { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024, VK_SHADER_STAGE_ALL, nullptr } // Max 1024 textures
        };

        VkDescriptorBindingFlags bindingsFlags[2] = { 0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT };
        VkDescriptorSetLayoutBindingFlagsCreateInfo layoutFlags{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
        layoutFlags.bindingCount = 2;
        layoutFlags.pBindingFlags = bindingsFlags;

        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.pNext = &layoutFlags;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = (uint32_t)bindings.size();
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &m_SceneDescriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create scene descriptor set layout!");
        }

        // Allocate the set
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_SceneDescriptorSetLayout;
        vkAllocateDescriptorSets(m_Context->GetDevice(), &allocInfo, &m_SceneDescriptorSet);

        CH_CORE_INFO("ResourceManager: Creating default magenta texture...");
        auto fallback = std::make_unique<Image>(m_Context->GetAllocator(), m_Context->GetDevice(), 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        
        uint8_t magenta[] = { 255, 0, 255, 255 };
        Buffer staging(m_Context->GetAllocator(), 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        staging.Update(magenta, 4);

        {
            CH_CORE_INFO("ResourceManager: Transitioning default texture layout...");
            ScopedCommandBuffer cmd(m_Context);
            VulkanUtils::TransitionImageLayout(cmd, fallback->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { 1, 1, 1 } };
            vkCmdCopyBufferToImage(cmd, staging.GetBuffer(), fallback->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            VulkanUtils::TransitionImageLayout(cmd, fallback->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }

        AddTexture(std::move(fallback), "Default");

        CH_CORE_INFO("ResourceManager: Creating default material...");
        CreateMaterial("Default");

        CH_CORE_INFO("ResourceManager: Syncing materials to GPU...");
        SyncMaterialsToGPU();
        CH_CORE_INFO("ResourceManager: Global Resources Initialized.");
    }

    void ResourceManager::ResetTransientDescriptorPool()
    {
        vkResetDescriptorPool(m_Context->GetDevice(), m_TransientDescriptorPool, 0);
    }

    GraphImage ResourceManager::CreateGraphImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageLayout initialLayout, VkSampleCountFlagBits samples)
    {
        if (width == 0 || height == 0) throw std::runtime_error("failed to create graph image: zero dimensions!");
        auto allocator = m_Context->GetAllocator();
        auto device = m_Context->GetDevice();
        GraphImage graphImage{};
        graphImage.width = width; graphImage.height = height; graphImage.format = format; graphImage.usage = usage;
        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.extent = { width, height, 1 }; imageInfo.mipLevels = 1; imageInfo.arrayLayers = 1; imageInfo.format = format; imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; imageInfo.usage = usage; imageInfo.samples = samples; imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo vmaAllocInfo{}; vmaAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(allocator, &imageInfo, &vmaAllocInfo, &graphImage.handle, &graphImage.allocation, nullptr) != VK_SUCCESS) throw std::runtime_error("failed to create graph image!");
        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = graphImage.handle; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = format; viewInfo.subresourceRange.aspectMask = VulkanUtils::IsDepthFormat(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT; viewInfo.subresourceRange.baseMipLevel = 0; viewInfo.subresourceRange.levelCount = 1; viewInfo.subresourceRange.baseArrayLayer = 0; viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &graphImage.view) != VK_SUCCESS) throw std::runtime_error("failed to create graph image view!");
        return graphImage;
    }

    void ResourceManager::DestroyGraphImage(GraphImage& image)
    {
        if (image.handle == VK_NULL_HANDLE) return;
        vkDestroyImageView(m_Context->GetDevice(), image.view, nullptr);
        vmaDestroyImage(m_Context->GetAllocator(), image.handle, image.allocation);
        image.handle = VK_NULL_HANDLE; image.view = VK_NULL_HANDLE;
    }

    void ResourceManager::SubmitResourceFree(std::function<void()>&& func)
    {
        if (s_Instance) s_Instance->m_ResourceFreeQueue[s_Instance->m_CurrentFrameIndex].push_back(std::move(func));
    }

    void ResourceManager::ClearResourceFreeQueue(uint32_t frameIndex)
    {
        for (auto& func : m_ResourceFreeQueue[frameIndex]) func();
        m_ResourceFreeQueue[frameIndex].clear();
    }

    TextureHandle ResourceManager::LoadTexture(const std::string& path)
    {
        if (m_TextureMap.count(path)) return m_TextureMap[path];
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) { CH_CORE_ERROR("Failed to load texture image: {}", path); return TextureHandle(0); }
        VkDeviceSize imageSize = texWidth * texHeight * 4;
        Buffer stagingBuffer(m_Context->GetAllocator(), imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        stagingBuffer.Update(pixels, imageSize);
        stbi_image_free(pixels);
        auto image = std::make_unique<Image>(m_Context->GetAllocator(), m_Context->GetDevice(), (uint32_t)texWidth, (uint32_t)texHeight, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        {
            ScopedCommandBuffer cmd(m_Context);
            VulkanUtils::TransitionImageLayout(cmd, image->GetImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)texWidth, (uint32_t)texHeight, 1 } };
            vkCmdCopyBufferToImage(cmd, stagingBuffer.GetBuffer(), image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            VulkanUtils::TransitionImageLayout(cmd, image->GetImage(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }
        return AddTexture(std::move(image), path);
    }

    TextureHandle ResourceManager::LoadHDRTexture(const std::string& path)
    {
        if (m_TextureMap.count(path)) return m_TextureMap[path];
        int texWidth, texHeight, texChannels;
        float* pixels = stbi_loadf(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) { CH_CORE_ERROR("Failed to load HDR texture image: {}", path); return TextureHandle(0); }
        VkDeviceSize imageSize = texWidth * texHeight * 4 * sizeof(float);
        Buffer stagingBuffer(m_Context->GetAllocator(), imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        stagingBuffer.Update(pixels, imageSize);
        stbi_image_free(pixels);
        auto image = std::make_unique<Image>(m_Context->GetAllocator(), m_Context->GetDevice(), (uint32_t)texWidth, (uint32_t)texHeight, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        {
            ScopedCommandBuffer cmd(m_Context);
            VulkanUtils::TransitionImageLayout(cmd, image->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            VkBufferImageCopy region{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)texWidth, (uint32_t)texHeight, 1 } };
            vkCmdCopyBufferToImage(cmd, stagingBuffer.GetBuffer(), image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            VulkanUtils::TransitionImageLayout(cmd, image->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }
        return AddTexture(std::move(image), path);
    }

    TextureHandle ResourceManager::AddTexture(std::unique_ptr<Image> texture, const std::string& name)
    {
        uint32_t index = (uint32_t)m_Textures.size();
        m_Textures.push_back(std::move(texture));
        m_TextureRefCount.push_back(1); 
        TextureHandle handle(index);
        if (!name.empty()) m_TextureMap[name] = handle;
        return handle;
    }

    MaterialHandle ResourceManager::CreateMaterial(const std::string& name)
    {
        return AddMaterial(std::make_unique<Material>(name.empty() ? "Unnamed Material" : name), name);
    }

    MaterialHandle ResourceManager::AddMaterial(std::unique_ptr<Material> material, const std::string& name)
    {
        uint32_t index = (uint32_t)m_Materials.size();
        m_Materials.push_back(std::move(material));
        m_MaterialRefCount.push_back(1);
        MaterialHandle handle(index);
        if (!name.empty()) m_MaterialMap[name] = handle;
        return handle;
    }

    void ResourceManager::SyncMaterialsToGPU()
    {
        if (m_Materials.empty()) return;
        VkDeviceSize bufferSize = std::max((VkDeviceSize)1024, sizeof(PBRMaterial) * m_Materials.size());
        if (!m_MaterialBuffer || m_MaterialBuffer->GetSize() < bufferSize) {
            m_MaterialBuffer = std::make_unique<Buffer>(m_Context->GetAllocator(), bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
        }
        std::vector<PBRMaterial> materialData;
        for (const auto& mat : m_Materials) {
            if (mat) materialData.push_back(mat->GetData());
            else materialData.push_back(PBRMaterial{}); // Fallback for deleted material
            if (mat) mat->ClearDirty();
        }
        Buffer staging(m_Context->GetAllocator(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        staging.UploadData(materialData.data(), sizeof(PBRMaterial) * materialData.size());
        {
            ScopedCommandBuffer cmd(m_Context);
            VkBufferCopy copy{ 0, 0, sizeof(PBRMaterial) * materialData.size() };
            vkCmdCopyBuffer(cmd, staging.GetBuffer(), m_MaterialBuffer->GetBuffer(), 1, &copy);
        }

        UpdateSceneDescriptorSet();
    }

    void ResourceManager::AddRef(TextureHandle handle) { if (handle.id < m_TextureRefCount.size()) m_TextureRefCount[handle.id]++; }
    void ResourceManager::Release(TextureHandle handle) { if (handle.id < m_TextureRefCount.size() && m_TextureRefCount[handle.id] > 0) { if (--m_TextureRefCount[handle.id] == 0 && handle.id != 0) { Image* raw = m_Textures[handle.id].release(); SubmitResourceFree([raw](){ delete raw; }); } } }
    uint32_t ResourceManager::GetRefCount(TextureHandle handle) { return handle.id < m_TextureRefCount.size() ? m_TextureRefCount[handle.id] : 0; }
    void ResourceManager::AddRef(BufferHandle handle) { if (handle.id < m_BufferRefCount.size()) m_BufferRefCount[handle.id]++; }
    void ResourceManager::Release(BufferHandle handle) { if (handle.id < m_BufferRefCount.size() && m_BufferRefCount[handle.id] > 0) { if (--m_BufferRefCount[handle.id] == 0) { Buffer* raw = m_Buffers[handle.id].release(); SubmitResourceFree([raw](){ delete raw; }); } } }
    uint32_t ResourceManager::GetRefCount(BufferHandle handle) { return handle.id < m_BufferRefCount.size() ? m_BufferRefCount[handle.id] : 0; }
    void ResourceManager::AddRef(MaterialHandle handle) { if (handle.id < m_MaterialRefCount.size()) m_MaterialRefCount[handle.id]++; }
    void ResourceManager::Release(MaterialHandle handle) { if (handle.id < m_MaterialRefCount.size() && m_MaterialRefCount[handle.id] > 0) { if (--m_MaterialRefCount[handle.id] == 0 && handle.id != 0) { Material* raw = m_Materials[handle.id].release(); SubmitResourceFree([raw](){ delete raw; }); } } }
    TextureHandle ResourceManager::GetTextureIndex(const std::string& name) { return m_TextureMap.count(name) ? m_TextureMap[name] : TextureHandle(0); }

    void ResourceManager::CreateDescriptorPool()
    {
        std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 }, { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 }, { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT, 1000, (uint32_t)poolSizes.size(), poolSizes.data() };
        if (vkCreateDescriptorPool(m_Context->GetDevice(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) throw std::runtime_error("failed to create descriptor pool!");
    }

    void ResourceManager::CreateTransientDescriptorPool()
    {
        std::vector<VkDescriptorPoolSize> pool_sizes = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4000 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4000 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4000 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4000 }, { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1000 } };
        VkDescriptorPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 4000, (uint32_t)pool_sizes.size(), pool_sizes.data() };
        if (vkCreateDescriptorPool(m_Context->GetDevice(), &pool_info, nullptr, &m_TransientDescriptorPool) != VK_SUCCESS) throw std::runtime_error("failed to create transient descriptor pool!");
    }

    void ResourceManager::CreateTextureSampler()
    {
        VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.magFilter = VK_FILTER_LINEAR; samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT; samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE; samplerInfo.maxAnisotropy = m_Context->GetDeviceProperties().limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK; samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE; samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS; samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        if (vkCreateSampler(m_Context->GetDevice(), &samplerInfo, nullptr, &m_TextureSampler) != VK_SUCCESS) throw std::runtime_error("failed to create texture sampler!");
    }

    Image* ResourceManager::GetTexture(TextureHandle handle) {
        if (handle.id < m_Textures.size() && m_Textures[handle.id]) return m_Textures[handle.id].get();
        return m_Textures[0].get(); // Fallback to Default
    }

    Material* ResourceManager::GetMaterial(MaterialHandle handle) {
        if (handle.id < m_Materials.size() && m_Materials[handle.id]) return m_Materials[handle.id].get();
        return m_Materials[0].get(); // Fallback to Default
    }

    Buffer* ResourceManager::GetBuffer(BufferHandle handle) {
        if (handle.id < m_Buffers.size() && m_Buffers[handle.id]) return m_Buffers[handle.id].get();
        return nullptr;
    }

    void AddRefInternal(Handle<Image> handle) { if (ResourceManager::Get()) ResourceManager::Get()->AddRef(handle); }
    void ReleaseInternal(Handle<Image> handle) { if (ResourceManager::Get()) ResourceManager::Get()->Release(handle); }
    void AddRefInternal(Handle<Buffer> handle) { if (ResourceManager::Get()) ResourceManager::Get()->AddRef(handle); }
    void ReleaseInternal(Handle<Buffer> handle) { if (ResourceManager::Get()) ResourceManager::Get()->Release(handle); }
    void AddRefInternal(Handle<Material> handle) { if (ResourceManager::Get()) ResourceManager::Get()->AddRef(handle); }
    void ReleaseInternal(Handle<Material> handle) { if (ResourceManager::Get()) ResourceManager::Get()->Release(handle); }

}
