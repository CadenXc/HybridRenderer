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
#include "Renderer/Backend/ShaderCommon.h"
#include "Scene/Scene.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Chimera
{
    ResourceManager* ResourceManager::s_Instance = nullptr;

    ResourceManager::ResourceManager()
    {
        s_Instance = this;
        m_ResourceFreeQueue.resize(MAX_FRAMES_IN_FLIGHT);
        m_SceneDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
        CH_CORE_INFO("ResourceManager: Initialized with {} frames in flight.", MAX_FRAMES_IN_FLIGHT);
    }

    ResourceManager::~ResourceManager()
    {
        VkDevice device = VulkanContext::Get().GetDevice();
        vkDeviceWaitIdle(device);

        m_MaterialBuffer.reset();
        m_Textures.clear();
        m_Materials.clear();
        m_Buffers.clear();

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) ClearResourceFreeQueue(i);

        if (m_SceneDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, m_SceneDescriptorSetLayout, nullptr);
        if (m_DescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
        for (auto pool : m_TransientDescriptorPools) if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
        if (m_TextureSampler != VK_NULL_HANDLE) vkDestroySampler(device, m_TextureSampler, nullptr);

        s_Instance = nullptr;
    }

    void ResourceManager::InitGlobalResources()
    {
        CreateDescriptorPool();
        CreateTransientDescriptorPools();
        CreateTextureSampler();
        CreateSceneDescriptorSetLayout();
        AllocatePersistentSets();
        CreateDefaultResources();
    }

    void ResourceManager::ResetTransientDescriptorPool()
    {
        VkDescriptorPool currentPool = m_TransientDescriptorPools[m_CurrentFrameIndex];
        if (currentPool != VK_NULL_HANDLE) vkResetDescriptorPool(VulkanContext::Get().GetDevice(), currentPool, 0);
    }

    void ResourceManager::CreateTransientDescriptorPools()
    {
        m_TransientDescriptorPools.resize(MAX_FRAMES_IN_FLIGHT);
        std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4000 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4000 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4000 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4000 }, { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 } };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 4000, (uint32_t)poolSizes.size(), poolSizes.data() };
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) vkCreateDescriptorPool(VulkanContext::Get().GetDevice(), &poolInfo, nullptr, &m_TransientDescriptorPools[i]);
    }

    void ResourceManager::CreateTextureSampler()
    {
        VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = VulkanContext::Get().GetDeviceProperties().limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        vkCreateSampler(VulkanContext::Get().GetDevice(), &samplerInfo, nullptr, &m_TextureSampler);
    }

    // --- Core Management Logic ---
    void ResourceManager::CreateDescriptorPool() { std::vector<VkDescriptorPoolSize> p = { {VK_DESCRIPTOR_TYPE_SAMPLER, 1000}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100} }; VkDescriptorPoolCreateInfo i{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT, 1000, (uint32_t)p.size(), p.data()}; vkCreateDescriptorPool(VulkanContext::Get().GetDevice(), &i, nullptr, &m_DescriptorPool); }
    void ResourceManager::CreateSceneDescriptorSetLayout() { std::vector<VkDescriptorSetLayoutBinding> b = { { 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_ALL, nullptr }, { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr }, { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr }, { 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024, VK_SHADER_STAGE_ALL, nullptr } }; VkDescriptorBindingFlags f[4] = { VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT }; VkDescriptorSetLayoutBindingFlagsCreateInfo lf{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, nullptr, 4, f }; VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &lf, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT, (uint32_t)b.size(), b.data() }; vkCreateDescriptorSetLayout(VulkanContext::Get().GetDevice(), &li, nullptr, &m_SceneDescriptorSetLayout); }
    void ResourceManager::AllocatePersistentSets() { VkDescriptorSetLayout eL = VulkanContext::Get().GetEmptyDescriptorSetLayout(); VkDescriptorSetAllocateInfo eA{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_DescriptorPool, 1, &eL }; vkAllocateDescriptorSets(VulkanContext::Get().GetDevice(), &eA, &VulkanContext::Get().GetEmptyDescriptorSetRef()); m_SceneDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT); std::vector<VkDescriptorSetLayout> ls(MAX_FRAMES_IN_FLIGHT, m_SceneDescriptorSetLayout); VkDescriptorSetAllocateInfo aI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_DescriptorPool, MAX_FRAMES_IN_FLIGHT, ls.data() }; vkAllocateDescriptorSets(VulkanContext::Get().GetDevice(), &aI, m_SceneDescriptorSets.data()); }
    void ResourceManager::CreateDefaultResources() { auto f = std::make_unique<Image>(1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT); uint8_t m[] = { 255, 0, 255, 255 }; Buffer s(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY); s.Update(m, 4); { ScopedCommandBuffer c; VulkanUtils::TransitionImageLayout(c, (VkImage)f->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1); VkBufferImageCopy r{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { 1, 1, 1 } }; vkCmdCopyBufferToImage(c, (VkBuffer)s.GetBuffer(), (VkImage)f->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r); VulkanUtils::TransitionImageLayout(c, (VkImage)f->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1); } AddTexture(std::move(f), "Default"); CreateMaterial("Default"); SyncMaterialsToGPU(); }
    void ResourceManager::UpdateGlobalResources(uint32_t cF, const UniformBufferObject& ubo) { m_CurrentFrameIndex = cF; }
    void ResourceManager::UpdateSceneDescriptorSet(Scene* s, uint32_t fI) 
    { 
        if (!s || m_SceneDescriptorSets.empty()) return; 
        
        // If fI is 0xFFFFFFFF, update ALL sets
        uint32_t start = (fI == 0xFFFFFFFF) ? 0 : fI;
        uint32_t end = (fI == 0xFFFFFFFF) ? (uint32_t)m_SceneDescriptorSets.size() : fI + 1;

        for (uint32_t i = start; i < end; ++i)
        {
            VkDescriptorSet tS = m_SceneDescriptorSets[i]; 
            if (tS == VK_NULL_HANDLE) continue; 
            
            std::vector<VkWriteDescriptorSet> wS; 
            VkAccelerationStructureKHR tlas = s->GetTLAS(); 
            VkWriteDescriptorSetAccelerationStructureKHR aW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, nullptr, 1, &tlas }; 
            
            if (tlas != VK_NULL_HANDLE) { 
                VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &aW, tS, 0, 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR }; 
                wS.push_back(w); 
            } 
            
            if (m_MaterialBuffer && m_MaterialBuffer->GetBuffer() != VK_NULL_HANDLE) { 
                static VkDescriptorBufferInfo mI; 
                mI = { (VkBuffer)m_MaterialBuffer->GetBuffer(), 0, VK_WHOLE_SIZE }; 
                VkWriteDescriptorSet mW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, tS, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &mI }; 
                wS.push_back(mW); 
            } 
            
            auto iB = s->GetInstanceDataBuffer(); 
            if (iB != VK_NULL_HANDLE) { 
                static VkDescriptorBufferInfo iI; 
                iI = { (VkBuffer)iB, 0, VK_WHOLE_SIZE }; 
                VkWriteDescriptorSet iW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, tS, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &iI }; 
                wS.push_back(iW); 
            } 
            
            std::vector<VkDescriptorImageInfo> iIs; 
            for (uint32_t j = 0; j < (uint32_t)m_Textures.size(); ++j) { 
                if (m_Textures[j] && m_Textures[j]->GetImageView() != VK_NULL_HANDLE) 
                    iIs.push_back({ m_TextureSampler, m_Textures[j]->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }); 
            } 
            
            if (!iIs.empty()) { 
                VkWriteDescriptorSet tW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, tS, 3, 0, (uint32_t)std::min((size_t)1024, iIs.size()), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, iIs.data() }; 
                wS.push_back(tW); 
            } 
            
            if (!wS.empty()) vkUpdateDescriptorSets(VulkanContext::Get().GetDevice(), (uint32_t)wS.size(), wS.data(), 0, nullptr); 
        }
    }
    void ResourceManager::SyncMaterialsToGPU()
    {
        if (m_Materials.empty()) return;
        
        VkDeviceSize bufferSize = std::max((VkDeviceSize)1024, (VkDeviceSize)(sizeof(PBRMaterial) * m_Materials.size()));
        
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
            vkCmdCopyBuffer(cmd, (VkBuffer)staging.GetBuffer(), (VkBuffer)m_MaterialBuffer->GetBuffer(), 1, &copy);
        }
    }
    GraphImage ResourceManager::CreateGraphImage(uint32_t w, uint32_t h, VkFormat f, VkImageUsageFlags u, VkImageLayout iL, VkSampleCountFlagBits s) 
    { 
        static uint32_t s_ActiveGraphImages = 0;
        GraphImage i{}; 
        i.width = w; i.height = h; i.format = f; 
        
        bool isDepth = VulkanUtils::IsDepthFormat(f);

        // Smart usage flags: Only add STORAGE_BIT if it's NOT a depth format
        VkImageUsageFlags finalUsage = u | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        
        if (isDepth)
        {
            finalUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        }
        else
        {
            finalUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        }
        
        i.usage = finalUsage;

        VkImageCreateInfo iI{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D, f, { w, h, 1 }, 1, 1, s, VK_IMAGE_TILING_OPTIMAL, finalUsage, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED }; 
        VmaAllocationCreateInfo vA{ 0, VMA_MEMORY_USAGE_GPU_ONLY }; 
        
        VkResult res = vmaCreateImage(VulkanContext::Get().GetAllocator(), &iI, &vA, &i.handle, &i.allocation, nullptr);
        if (res != VK_SUCCESS)
        {
            CH_CORE_ERROR("ResourceManager: Failed to create GPU image! Active Images: {}, Format: {}, Usage: {}, Error Code: {}", s_ActiveGraphImages, (int)f, finalUsage, (int)res);
            return i;
        }
        s_ActiveGraphImages++;

        VkImageViewCreateInfo vW{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, i.handle, VK_IMAGE_VIEW_TYPE_2D, f, { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY }, { (VkImageAspectFlags)(isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1 } }; 
        
        res = vkCreateImageView(VulkanContext::Get().GetDevice(), &vW, nullptr, &i.view);
        if (res != VK_SUCCESS)
        {
            CH_CORE_ERROR("ResourceManager: Failed to create image view! Error Code: {}", (int)res);
            return i;
        }

        if (isDepth) 
        { 
            vW.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE }; 
            vkCreateImageView(VulkanContext::Get().GetDevice(), &vW, nullptr, &i.debug_view); 
        } 
        else 
        {
            i.debug_view = i.view; 
        }
        return i; 
    }

    void ResourceManager::DestroyGraphImage(GraphImage& i) 
    { 
        if (i.handle == VK_NULL_HANDLE)
        {
            return; 
        }
        vkDestroyImageView(VulkanContext::Get().GetDevice(), i.view, nullptr); 
        if (i.debug_view != i.view && i.debug_view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(VulkanContext::Get().GetDevice(), i.debug_view, nullptr); 
        }
        vmaDestroyImage(VulkanContext::Get().GetAllocator(), i.handle, i.allocation); 
        i.handle = VK_NULL_HANDLE; 
    }
    TextureHandle ResourceManager::LoadTexture(const std::string& p, bool srgb) 
    { 
        if (m_TextureMap.count(p)) return m_TextureMap[p]; 
        int tw, th, tc; 
        stbi_uc* px = stbi_load(p.c_str(), &tw, &th, &tc, STBI_rgb_alpha); 
        if (!px) return TextureHandle(); 
        
        VkDeviceSize s = tw * th * 4; 
        Buffer st(s, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU); 
        st.Update(px, s); 
        stbi_image_free(px); 
        
        VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        
        auto im = std::make_unique<Image>((uint32_t)tw, (uint32_t)th, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT); 
        { 
            ScopedCommandBuffer c; 
            VulkanUtils::TransitionImageLayout(c, (VkImage)im->GetImage(), format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1); 
            VkBufferImageCopy r{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)tw, (uint32_t)th, 1 } }; 
            vkCmdCopyBufferToImage(c, (VkBuffer)st.GetBuffer(), (VkImage)im->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r); 
            VulkanUtils::TransitionImageLayout(c, (VkImage)im->GetImage(), format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1); 
        } 
        return AddTexture(std::move(im), p); 
    }
    TextureHandle ResourceManager::LoadHDRTexture(const std::string& p) { 
        if (m_TextureMap.count(p)) return m_TextureMap[p]; 
        
        CH_CORE_INFO("ResourceManager: stbi_loadf starting for: {}", p);
        int tw, th, tc; 
        float* px = stbi_loadf(p.c_str(), &tw, &th, &tc, STBI_rgb_alpha); 
        
        if (!px) {
            CH_CORE_ERROR("ResourceManager: Failed to load HDR texture via stbi: {}", p);
            return TextureHandle(); 
        }
        CH_CORE_INFO("ResourceManager: HDR loaded. Size: {}x{}, Channels: {}", tw, th, tc);

        VkDeviceSize s = (VkDeviceSize)tw * th * 4 * sizeof(float); 
        Buffer st(s, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU); 
        st.Update(px, s); 
        stbi_image_free(px); 

        CH_CORE_INFO("ResourceManager: Creating GPU Image for HDR...");
        auto im = std::make_unique<Image>((uint32_t)tw, (uint32_t)th, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT); 
        
        { 
            ScopedCommandBuffer c; 
            VulkanUtils::TransitionImageLayout(c, (VkImage)im->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1); 
            VkBufferImageCopy r{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)tw, (uint32_t)th, 1 } }; 
            vkCmdCopyBufferToImage(c, (VkBuffer)st.GetBuffer(), (VkImage)im->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r); 
            VulkanUtils::TransitionImageLayout(c, (VkImage)im->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1); 
        } 
        
        CH_CORE_INFO("ResourceManager: HDR texture registered successfully.");
        return AddTexture(std::move(im), p); 
    }
    TextureHandle ResourceManager::AddTexture(std::unique_ptr<Image> t, const std::string& n)
    {
        uint32_t idx = (uint32_t)m_Textures.size();
        m_Textures.push_back(std::move(t));
        m_TextureRefCount.push_back(1);
        if (!n.empty())
        {
            m_TextureMap[n] = TextureHandle(idx);
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
            SubmitResourceFree([r]() { delete r; });
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
            m_Buffers[h.id].reset();
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
            SubmitResourceFree([r]() { delete r; });
        }
    }

    void ResourceManager::SubmitResourceFree(std::function<void()>&& f)
    {
        if (s_Instance)
        {
            if (s_Instance->m_CurrentFrameIndex >= s_Instance->m_ResourceFreeQueue.size())
            {
                CH_CORE_ERROR("ResourceManager: Frame index {} out of range for FreeQueue (size {})!", s_Instance->m_CurrentFrameIndex, s_Instance->m_ResourceFreeQueue.size());
                return;
            }
            s_Instance->m_ResourceFreeQueue[s_Instance->m_CurrentFrameIndex].push_back(std::move(f));
        }
    }

    void ResourceManager::ClearResourceFreeQueue(uint32_t fI)
    {
        if (fI < m_ResourceFreeQueue.size())
        {
            for (auto& f : m_ResourceFreeQueue[fI])
            {
                f();
            }
            m_ResourceFreeQueue[fI].clear();
        }

        // --- FIX: Clear transient buffers for THIS frame to prevent memory leak ---
        if (fI < MAX_FRAMES_IN_FLIGHT)
        {
            m_TransientBuffers[fI].clear();
        }
    }

    Image* ResourceManager::GetTexture(TextureHandle h)
    {
        if (h.id < m_Textures.size() && m_Textures[h.id])
        {
            return m_Textures[h.id].get();
        }
        return m_Textures.empty() ? nullptr : m_Textures[0].get();
    }

    Material* ResourceManager::GetMaterial(MaterialHandle h)
    {
        if (h.id < m_Materials.size() && m_Materials[h.id])
        {
            return m_Materials[h.id].get();
        }
        return m_Materials.empty() ? nullptr : m_Materials[0].get();
    }

    Buffer* ResourceManager::GetBuffer(BufferHandle h)
    {
        return (h.id < m_Buffers.size() && m_Buffers[h.id]) ? m_Buffers[h.id].get() : nullptr;
    }

    TextureHandle ResourceManager::GetTextureIndex(const std::string& n)
    {
        return m_TextureMap.count(n) ? m_TextureMap[n] : TextureHandle();
    }

    // [FIX] 补全外部辅助函数
    void AddRefInternal(Handle<Image> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().AddRef(h); }
    void ReleaseInternal(Handle<Image> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().Release(h); }
    void AddRefInternal(Handle<Buffer> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().AddRef(h); }
    void ReleaseInternal(Handle<Buffer> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().Release(h); }
    void AddRefInternal(Handle<Material> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().AddRef(h); }
    void ReleaseInternal(Handle<Material> h) { if (ResourceManager::HasInstance()) ResourceManager::Get().Release(h); }
}
