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
#include "Scene/Model.h"

#include "stb_image.h"

namespace Chimera
{
    ResourceManager* ResourceManager::s_Instance = nullptr;

    ResourceManager::ResourceManager()
    {
        if (s_Instance)
        {
            CH_CORE_ERROR("ResourceManager: Multiple instances detected!");
        }
        s_Instance = this;
        m_IsCleared = false;
        m_Context = Application::Get().GetContext();
        m_ResourceFreeQueue.resize(MAX_FRAMES_IN_FLIGHT);
        m_SceneDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
        CH_CORE_INFO("ResourceManager: Initialized. m_Context count: {}", m_Context.use_count());
    }

    ResourceManager::~ResourceManager()
    {
        CH_CORE_INFO("ResourceManager: Destructor started.");
        Clear();
        m_Context.reset();
        s_Instance = nullptr;
        CH_CORE_INFO("ResourceManager: Destructor finished.");
    }

    void ResourceManager::Clear()
    {
        if (m_IsCleared || !m_Context)
        {
            CH_CORE_INFO("ResourceManager: Clear() skipped (m_IsCleared: {}, m_Context: {})", m_IsCleared, (bool)m_Context);
            return;
        }
        
        m_IsCleared = true;
        CH_CORE_INFO("ResourceManager: Clear() executing deep cleanup...");
        
        VkDevice device = m_Context->GetDevice();
        vkDeviceWaitIdle(device);

        // 1. Scene & Models
        if (m_ActiveScene)
        {
            CH_CORE_INFO("ResourceManager: Dropping Active Scene...");
            m_ActiveScene.reset();
        }
        
        // 2. Clear Asset Maps
        CH_CORE_INFO("ResourceManager: Clearing {} Materials...", m_Materials.size());
        m_Materials.clear();
        m_MaterialMap.clear();
        m_MaterialRefCount.clear();
        
        CH_CORE_INFO("ResourceManager: Clearing {} Textures...", m_Textures.size());
        for (size_t i = 0; i < m_Textures.size(); ++i)
        {
            if (m_Textures[i])
            {
                CH_CORE_INFO("ResourceManager: Destroying texture [ID: {}]", (uint32_t)i);
                m_Textures[i].reset();
            }
        }
        m_Textures.clear(); 
        m_TextureMap.clear();
        m_TextureRefCount.clear();
        
        CH_CORE_INFO("ResourceManager: Clearing {} Buffers...", m_Buffers.size());
        m_Buffers.clear();
        m_BufferRefCount.clear();

        CH_CORE_INFO("ResourceManager: Clearing {} Uniform Buffers...", m_UniformBuffers.size());
        m_UniformBuffers.clear();

        // 3. System Buffers
        CH_CORE_INFO("ResourceManager: Resetting system buffers...");
        if (m_MaterialBuffer)
        {
            CH_CORE_INFO("ResourceManager: Resetting MaterialBuffer...");
            m_MaterialBuffer.reset();
        }
        
        if (m_PrimitiveBuffer)
        {
            CH_CORE_INFO("ResourceManager: Resetting PrimitiveBuffer...");
            m_PrimitiveBuffer.reset();
        }

        CH_CORE_INFO("ResourceManager: Clearing ResourceFreeQueues...");
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            ClearResourceFreeQueue(i);
        }

        // 4. Descriptors & Samplers
        CH_CORE_INFO("ResourceManager: Destroying Descriptors and Samplers...");
        if (m_SceneDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, m_SceneDescriptorSetLayout, nullptr);
            m_SceneDescriptorSetLayout = VK_NULL_HANDLE;
        }
        
        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }

        for (auto& pool : m_TransientDescriptorPools)
        {
            if (pool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device, pool, nullptr);
                pool = VK_NULL_HANDLE;
            }
        }

        if (m_TextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, m_TextureSampler, nullptr);
            m_TextureSampler = VK_NULL_HANDLE;
        }
        
        CH_CORE_INFO("ResourceManager: Clear() finished.");
    }

    void ResourceManager::ClearRuntimeAssets()
    {
        CH_CORE_INFO("ResourceManager: Clearing runtime assets...");
        if (!m_Context) return;
        
        vkDeviceWaitIdle(m_Context->GetDevice());

        // 1. Clear Materials (Except ID 0 - Default)
        if (m_Materials.size() > 1)
        {
            auto defaultMat = std::move(m_Materials[0]);
            m_Materials.clear();
            m_Materials.push_back(std::move(defaultMat));
            
            m_MaterialMap.clear();
            m_MaterialMap["Default"] = MaterialHandle(0);
            
            m_MaterialRefCount.clear();
            m_MaterialRefCount.push_back(1);
        }

        // 2. Clear Textures (Except ID 0: Default and ID 1: BlueNoise)
        uint32_t systemTextureCount = (m_Textures.size() >= 2) ? 2 : (uint32_t)m_Textures.size();
        if (m_Textures.size() > systemTextureCount)
        {
            std::vector<std::unique_ptr<Image>> systemTextures;
            for (uint32_t i = 0; i < systemTextureCount; ++i)
            {
                systemTextures.push_back(std::move(m_Textures[i]));
            }

            m_Textures.clear();
            for (auto& t : systemTextures)
            {
                m_Textures.push_back(std::move(t));
            }

            // Restore Map
            m_TextureMap.clear();
            m_TextureMap["Default"] = TextureHandle(0);
            
            m_TextureRefCount.clear();
            m_TextureRefCount.resize(systemTextureCount, 1);
        }

        // 3. [CRITICAL] Clear m_Buffers
        m_Buffers.clear();
        m_BufferRefCount.clear();

        // 4. Flush Deletion Queues
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            ClearResourceFreeQueue(i);
        }

        CH_CORE_INFO("ResourceManager: Runtime assets cleared. Preserved system textures.");
    }

    void ResourceManager::InitGlobalResources()
    {
        m_MaterialBuffer = std::make_unique<Buffer>(sizeof(GpuMaterial) * 1024, 
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
            VMA_MEMORY_USAGE_CPU_TO_GPU, "Global_MaterialBuffer");

        m_PrimitiveBuffer = std::make_unique<Buffer>(sizeof(GpuPrimitive) * 4096, 
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
            VMA_MEMORY_USAGE_GPU_ONLY, "Global_PrimitiveBuffer");

        CreateTextureSampler();
        CreateDescriptorPool();
        CreateTransientDescriptorPools();
        CreateSceneDescriptorSetLayout();
        AllocatePersistentSets();
        CreateDefaultResources();
    }

    void ResourceManager::ResetTransientDescriptorPool()
    {
        VkDescriptorPool currentPool = m_TransientDescriptorPools[m_CurrentFrameIndex];
        if (currentPool != VK_NULL_HANDLE)
        {
            vkResetDescriptorPool(m_Context->GetDevice(), currentPool, 0);
        }
    }

    void ResourceManager::CreateTransientDescriptorPools()
    {
        m_TransientDescriptorPools.resize(MAX_FRAMES_IN_FLIGHT);
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4000 },
            { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 }
        };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 4000;
        poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            vkCreateDescriptorPool(m_Context->GetDevice(), &poolInfo, nullptr, &m_TransientDescriptorPools[i]);
        }
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
        samplerInfo.maxAnisotropy = m_Context->GetDeviceProperties().limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        vkCreateSampler(m_Context->GetDevice(), &samplerInfo, nullptr, &m_TextureSampler);
    }

    void ResourceManager::CreateDescriptorPool()
    {
        std::vector<VkDescriptorPoolSize> p = {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100}
        };
        VkDescriptorPoolCreateInfo i{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        i.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        i.maxSets = 1000;
        i.poolSizeCount = (uint32_t)p.size();
        i.pPoolSizes = p.data();
        vkCreateDescriptorPool(m_Context->GetDevice(), &i, nullptr, &m_DescriptorPool);
    }

    void ResourceManager::CreateSceneDescriptorSetLayout()
    {
        std::vector<VkDescriptorSetLayoutBinding> b = {
            { BINDING_AS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_ALL, nullptr },
            { BINDING_MATERIALS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr },
            { BINDING_PRIMITIVES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr }, 
            { BINDING_TEXTURES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024, VK_SHADER_STAGE_ALL, nullptr }
        };
        VkDescriptorBindingFlags f[4] = {
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
        };
        VkDescriptorSetLayoutBindingFlagsCreateInfo lf{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, nullptr, 4, f };
        VkDescriptorSetLayoutCreateInfo li{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &lf, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT, (uint32_t)b.size(), b.data() };
        vkCreateDescriptorSetLayout(m_Context->GetDevice(), &li, nullptr, &m_SceneDescriptorSetLayout);
    }

    void ResourceManager::AllocatePersistentSets()
    {
        VkDescriptorSetLayout eL = m_Context->GetEmptyDescriptorSetLayout();
        VkDescriptorSetAllocateInfo eA{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_DescriptorPool, 1, &eL };
        vkAllocateDescriptorSets(m_Context->GetDevice(), &eA, &m_Context->GetEmptyDescriptorSetRef());
        m_SceneDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        std::vector<VkDescriptorSetLayout> ls(MAX_FRAMES_IN_FLIGHT, m_SceneDescriptorSetLayout);
        VkDescriptorSetAllocateInfo aI{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_DescriptorPool, MAX_FRAMES_IN_FLIGHT, ls.data() };
        vkAllocateDescriptorSets(m_Context->GetDevice(), &aI, m_SceneDescriptorSets.data());
    }

    void ResourceManager::CreateDefaultResources()
    {
        auto f = std::make_unique<Image>(1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, "Texture_Default");
        uint8_t m[] = { 0, 0, 0, 255 }; // Use BLACK as safe default instead of PINK
        Buffer s(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, "Staging_DefaultTexture");
        s.Update(m, 4);
        {
            ScopedCommandBuffer c;
            VulkanUtils::TransitionImageLayout(c, (VkImage)f->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            VkBufferImageCopy r{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { 1, 1, 1 } };
            vkCmdCopyBufferToImage(c, (VkBuffer)s.GetBuffer(), (VkImage)f->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
            VulkanUtils::TransitionImageLayout(c, (VkImage)f->GetImage(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }
        AddTexture(std::move(f), "Default");
        CreateMaterial("Default");
        SyncMaterialsToGPU();
    }

    void ResourceManager::UpdateGlobalResources(uint32_t cF, const UniformBufferObject& ubo)
    {
        m_CurrentFrameIndex = cF;
    }

    void ResourceManager::UpdateSceneDescriptorSet(Scene* s, uint32_t fI)
    {
        if (!s || m_SceneDescriptorSets.empty())
        {
            return;
        }

        uint32_t start = (fI == 0xFFFFFFFF) ? 0 : fI;
        uint32_t end = (fI == 0xFFFFFFFF) ? (uint32_t)m_SceneDescriptorSets.size() : fI + 1;

        for (uint32_t i = start; i < end; ++i)
        {
            VkDescriptorSet tS = m_SceneDescriptorSets[i];
            if (tS == VK_NULL_HANDLE)
            {
                continue;
            }

            std::vector<VkWriteDescriptorSet> wS;
            VkAccelerationStructureKHR tlas = s->GetTLAS();
            VkWriteDescriptorSetAccelerationStructureKHR aW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, nullptr, 1, &tlas };

            if (tlas != VK_NULL_HANDLE)
            {
                VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &aW, tS, BINDING_AS, 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR };
                wS.push_back(w);
            }

            if (m_MaterialBuffer && m_MaterialBuffer->GetBuffer() != VK_NULL_HANDLE)
            {
                static VkDescriptorBufferInfo mI;
                mI = { (VkBuffer)m_MaterialBuffer->GetBuffer(), 0, VK_WHOLE_SIZE };
                VkWriteDescriptorSet mW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, tS, BINDING_MATERIALS, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &mI };
                wS.push_back(mW);
            }

            if (m_PrimitiveBuffer && m_PrimitiveBuffer->GetBuffer() != VK_NULL_HANDLE)
            {
                static VkDescriptorBufferInfo iI;
                iI = { (VkBuffer)m_PrimitiveBuffer->GetBuffer(), 0, VK_WHOLE_SIZE };
                VkWriteDescriptorSet iW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, tS, BINDING_PRIMITIVES, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &iI };
                wS.push_back(iW);
            }

            std::vector<VkDescriptorImageInfo> iIs;
            for (uint32_t j = 0; j < (uint32_t)m_Textures.size(); ++j)
            {
                if (m_Textures[j] && m_Textures[j]->GetImageView() != VK_NULL_HANDLE)
                {
                    iIs.push_back({ m_TextureSampler, m_Textures[j]->GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
                }
            }

            if (!iIs.empty())
            {
                VkWriteDescriptorSet tW{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, tS, BINDING_TEXTURES, 0, (uint32_t)std::min((size_t)1024, iIs.size()), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, iIs.data() };
                wS.push_back(tW);
            }

            if (!wS.empty())
            {
                vkUpdateDescriptorSets(m_Context->GetDevice(), (uint32_t)wS.size(), wS.data(), 0, nullptr);
            }
        }
    }

    void ResourceManager::SyncPrimitivesToGPU(Scene* scene)
    {
        if (!scene || !m_PrimitiveBuffer)
        {
            return;
        }

        const auto& entities = scene->GetEntities();
        if (entities.empty())
        {
            return;
        }

        std::vector<GpuPrimitive> primitiveData;
        for (const auto& entity : entities)
        {
            if (!entity.mesh.model)
            {
                continue;
            }

            const auto& meshes = entity.mesh.model->GetMeshes();
            glm::mat4 modelMatrix = entity.transform.GetTransform();

            for (const auto& mesh : meshes)
            {
                GpuPrimitive gpuPrim{};
                gpuPrim.transform = modelMatrix * mesh.transform;
                gpuPrim.normalMatrix = glm::transpose(glm::inverse(gpuPrim.transform));
                gpuPrim.prevTransform = entity.prevTransform * mesh.transform;
                gpuPrim.materialIndex = mesh.materialIndex;
                gpuPrim.vertexAddress = entity.mesh.model->GetVertexBuffer()->GetDeviceAddress() + (mesh.vertexOffset * sizeof(GpuVertex));
                gpuPrim.indexAddress = entity.mesh.model->GetIndexBuffer()->GetDeviceAddress() + (mesh.indexOffset * sizeof(uint32_t));
                primitiveData.push_back(gpuPrim);
            }
        }

        if (primitiveData.empty())
        {
            return;
        }

        VkDeviceSize dataSize = primitiveData.size() * sizeof(GpuPrimitive);
        if (m_PrimitiveBuffer->GetSize() < dataSize)
        {
            m_PrimitiveBuffer = std::make_unique<Buffer>(dataSize * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, "Global_PrimitiveBuffer_Resized");
        }

        Buffer staging(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, "Staging_SyncPrimitives");
        staging.UploadData(primitiveData.data(), dataSize);

        {
            ScopedCommandBuffer cmd;
            VkBufferCopy copy{ 0, 0, dataSize };
            vkCmdCopyBuffer(cmd, (VkBuffer)staging.GetBuffer(), (VkBuffer)m_PrimitiveBuffer->GetBuffer(), 1, &copy);
        }
    }

    void ResourceManager::UpdateMaterial(uint32_t materialIndex, const GpuMaterial& material)
    {
        if (materialIndex < m_Materials.size())
        {
            m_Materials[materialIndex]->SetData(material);
            if (m_MaterialBuffer)
            {
                m_MaterialBuffer->Update(&material, sizeof(GpuMaterial), materialIndex * sizeof(GpuMaterial));
            }
        }
    }

    void ResourceManager::SyncMaterialsToGPU()
    {
        if (m_Materials.empty())
        {
            return;
        }

        VkDeviceSize bufferSize = std::max((VkDeviceSize)1024, (VkDeviceSize)(sizeof(GpuMaterial) * m_Materials.size()));

        if (!m_MaterialBuffer || m_MaterialBuffer->GetSize() < bufferSize)
        {
            m_MaterialBuffer = std::make_unique<Buffer>(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, "Global_MaterialBuffer_Resized");
        }

        std::vector<GpuMaterial> materialData;
        for (const auto& mat : m_Materials)
        {
            materialData.push_back(mat ? mat->GetData() : GpuMaterial{});
        }

        Buffer staging(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, "Staging_SyncMaterials");
        staging.UploadData(materialData.data(), sizeof(GpuMaterial) * materialData.size());

        {
            ScopedCommandBuffer cmd;
            VkBufferCopy copy{ 0, 0, sizeof(GpuMaterial) * materialData.size() };
            vkCmdCopyBuffer(cmd, (VkBuffer)staging.GetBuffer(), (VkBuffer)m_MaterialBuffer->GetBuffer(), 1, &copy);
        }
    }

    GraphImage ResourceManager::CreateGraphImage(uint32_t w, uint32_t h, VkFormat f, VkImageUsageFlags u, VkImageLayout iL, VkSampleCountFlagBits s, const std::string& name)
    {
        GraphImage i{};
        i.width = w;
        i.height = h;
        i.format = f;
        
        bool isDepth = VulkanUtils::IsDepthFormat(f);
        VkImageUsageFlags finalUsage = u | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        i.usage = finalUsage;
        VkImageCreateInfo iI{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D, f, { w, h, 1 }, 1, 1, s, VK_IMAGE_TILING_OPTIMAL, finalUsage, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED };
        VmaAllocationCreateInfo vA{ 0, VMA_MEMORY_USAGE_GPU_ONLY };

        vmaCreateImage(m_Context->GetAllocator(), &iI, &vA, &i.handle, &i.allocation, nullptr);

        if (!name.empty())
        {
            m_Context->SetDebugName((uint64_t)i.handle, VK_OBJECT_TYPE_IMAGE, name.c_str());
        }

        VkImageViewCreateInfo vW{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, i.handle, VK_IMAGE_VIEW_TYPE_2D, f, { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY }, { (VkImageAspectFlags)(isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1 } };
        vkCreateImageView(m_Context->GetDevice(), &vW, nullptr, &i.view);

        if (isDepth)
        {
            vW.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE };
            vkCreateImageView(m_Context->GetDevice(), &vW, nullptr, &i.debug_view);
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
        
        VkDevice device = m_Context->GetDevice();
        CH_CORE_INFO("ResourceManager: Destroying GraphImage [0x{:x}].", (uint64_t)i.handle);

        std::set<VkImageView> uniqueViews;
        if (i.view != VK_NULL_HANDLE) uniqueViews.insert(i.view);
        if (i.debug_view != VK_NULL_HANDLE) uniqueViews.insert(i.debug_view);
        
        for (auto v : uniqueViews)
        {
            vkDestroyImageView(device, v, nullptr);
        }
        
        i.view = VK_NULL_HANDLE;
        i.debug_view = VK_NULL_HANDLE;
        
        if (i.allocation != nullptr)
        {
            vmaDestroyImage(m_Context->GetAllocator(), i.handle, i.allocation);
            i.allocation = nullptr;
        }
        
        i.handle = VK_NULL_HANDLE;
    }

    TextureHandle ResourceManager::LoadTexture(const std::string& p, bool srgb)
    {
        if (m_TextureMap.count(p)) return m_TextureMap[p];
        int tw, th, tc;
        stbi_uc* px = stbi_load(p.c_str(), &tw, &th, &tc, STBI_rgb_alpha);
        if (!px) return TextureHandle();
        VkDeviceSize s = tw * th * 4;
        Buffer st(s, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, "Staging_LoadTexture");
        st.Update(px, s);
        stbi_image_free(px);
        VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        auto im = std::make_unique<Image>((uint32_t)tw, (uint32_t)th, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, "Texture_" + p);
        {
            ScopedCommandBuffer c;
            VulkanUtils::TransitionImageLayout(c, (VkImage)im->GetImage(), format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            VkBufferImageCopy r{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)tw, (uint32_t)th, 1 } };
            vkCmdCopyBufferToImage(c, (VkBuffer)st.GetBuffer(), (VkImage)im->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
            VulkanUtils::TransitionImageLayout(c, (VkImage)im->GetImage(), format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }
        return AddTexture(std::move(im), p);
    }

    TextureHandle ResourceManager::LoadHDRTexture(const std::string& p)
    {
        if (m_TextureMap.count(p)) return m_TextureMap[p];
        int tw, th, tc;
        // Load as RGBA float to maintain 4-channel alignment
        float* px = stbi_loadf(p.c_str(), &tw, &th, &tc, STBI_rgb_alpha);
        if (!px) 
        {
            CH_CORE_ERROR("ResourceManager: Failed to load HDR texture: {}", p);
            return TextureHandle();
        }

        VkDeviceSize s = (VkDeviceSize)tw * th * 4 * sizeof(float);
        Buffer st(s, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, "Staging_LoadHDRTexture");
        st.UploadData(px, s);
        stbi_image_free(px);

        auto im = std::make_unique<Image>((uint32_t)tw, (uint32_t)th, VK_FORMAT_R32G32B32A32_SFLOAT, 
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 
            VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, "Texture_HDR_" + p);
        
        {
            ScopedCommandBuffer c;
            VulkanUtils::TransitionImageLayout(c, (VkImage)im->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
            VkBufferImageCopy r{ 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { 0, 0, 0 }, { (uint32_t)tw, (uint32_t)th, 1 } };
            vkCmdCopyBufferToImage(c, (VkBuffer)st.GetBuffer(), (VkImage)im->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
            VulkanUtils::TransitionImageLayout(c, (VkImage)im->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
        }
        return AddTexture(std::move(im), p);
    }

    TextureHandle ResourceManager::AddTexture(std::unique_ptr<Image> t, const std::string& n)
    {
        uint32_t idx = (uint32_t)m_Textures.size();
        CH_CORE_INFO("ResourceManager: Registered Texture [ID: {}, Name: {}].", idx, n);
        m_Textures.push_back(std::move(t));
        m_TextureRefCount.push_back(1);
        if (!n.empty()) m_TextureMap[n] = TextureHandle(idx);
        return TextureHandle(idx);
    }

    MaterialHandle ResourceManager::CreateMaterial(const std::string& n)
    {
        return AddMaterial(std::make_unique<Material>(n), n);
    }

    MaterialHandle ResourceManager::AddMaterial(std::unique_ptr<Material> m, const std::string& n)
    {
        uint32_t idx = (uint32_t)m_Materials.size();
        CH_CORE_INFO("ResourceManager: Registered Material [ID: {}, Name: {}].", idx, n);
        m_Materials.push_back(std::move(m));
        m_MaterialRefCount.push_back(1);
        if (!n.empty()) m_MaterialMap[n] = MaterialHandle(idx);
        return MaterialHandle(idx);
    }

    void ResourceManager::AddRef(TextureHandle h)
    {
        if (h.id < m_TextureRefCount.size()) m_TextureRefCount[h.id]++;
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
        if (h.id < m_BufferRefCount.size()) m_BufferRefCount[h.id]++;
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
        if (h.id < m_MaterialRefCount.size()) m_MaterialRefCount[h.id]++;
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
            s_Instance->m_ResourceFreeQueue[s_Instance->m_CurrentFrameIndex].push_back(std::move(f));
        }
    }

    void ResourceManager::ClearResourceFreeQueue(uint32_t fI)
    {
        if (fI < m_ResourceFreeQueue.size())
        {
            for (auto& f : m_ResourceFreeQueue[fI]) f();
            m_ResourceFreeQueue[fI].clear();
        }
        if (fI < MAX_FRAMES_IN_FLIGHT) m_TransientBuffers[fI].clear();
    }

    Image* ResourceManager::GetTexture(TextureHandle h)
    {
        if (h.id < m_Textures.size() && m_Textures[h.id]) return m_Textures[h.id].get();
        return m_Textures.empty() ? nullptr : m_Textures[0].get();
    }

    Material* ResourceManager::GetMaterial(MaterialHandle h)
    {
        if (h.id < m_Materials.size() && m_Materials[h.id]) return m_Materials[h.id].get();
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

    void AddRefInternal(Handle<Image> h) { ResourceManager::Get().AddRef(h); }
    void ReleaseInternal(Handle<Image> h) { ResourceManager::Get().Release(h); }
    void AddRefInternal(Handle<Buffer> h) { ResourceManager::Get().AddRef(h); }
    void ReleaseInternal(Handle<Buffer> h) { ResourceManager::Get().Release(h); }
    void AddRefInternal(Handle<Material> h) { ResourceManager::Get().AddRef(h); }
    void ReleaseInternal(Handle<Material> h) { ResourceManager::Get().Release(h); }
}
