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
#include "Renderer/Pipelines/RenderPath.h"
#include "Assets/AssetImporter.h"
#include "Core/TaskSystem.h"

#include "stb_image.h"
#include <random>

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
}

ResourceManager::~ResourceManager()
{
    Clear();
    m_Context.reset();
    s_Instance = nullptr;
}

void ResourceManager::Clear()
{
    if (m_IsCleared || !m_Context) return;
    m_IsCleared = true;
    VkDevice device = m_Context->GetDevice();
    vkDeviceWaitIdle(device);
    if (m_ActiveScene) m_ActiveScene.reset();
    m_Materials.clear();
    m_MaterialMap.clear();
    m_MaterialRefCount.clear();
    for (auto& t : m_Textures)
        if (t) t.reset();
    m_Textures.clear();
    m_TextureMap.clear();
    m_TextureRefCount.clear();
    m_Buffers.clear();
    m_BufferRefCount.clear();
    m_UniformBuffers.clear();
    if (m_MaterialBuffer) m_MaterialBuffer.reset();
    if (m_InstanceBuffer) m_InstanceBuffer.reset();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        ClearResourceFreeQueue(i);
    if (m_SceneDescriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, m_SceneDescriptorSetLayout,
                                     nullptr);
        m_SceneDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_DescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }
    for (auto& pool : m_TransientDescriptorPools)
        if (pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, pool, nullptr);
    if (m_TextureSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, m_TextureSampler, nullptr);
        m_TextureSampler = VK_NULL_HANDLE;
    }
    if (m_NearestSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, m_NearestSampler, nullptr);
        m_NearestSampler = VK_NULL_HANDLE;
    }
}

void ResourceManager::ClearRuntimeAssets()
{
    if (!m_Context) return;
    vkDeviceWaitIdle(m_Context->GetDevice());
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
    uint32_t sysCount =
        (m_Textures.size() >= 2) ? 2 : (uint32_t)m_Textures.size();
    if (m_Textures.size() > sysCount)
    {
        std::vector<std::unique_ptr<Image>> sys;
        for (uint32_t i = 0; i < sysCount; ++i)
            sys.push_back(std::move(m_Textures[i]));
        m_Textures.clear();
        for (auto& t : sys) m_Textures.push_back(std::move(t));
        m_TextureMap.clear();
        m_TextureMap["Default"] = TextureHandle(0);
        m_TextureRefCount.clear();
        m_TextureRefCount.resize(sysCount, 1);
    }
    m_Buffers.clear();
    m_BufferRefCount.clear();
    if (m_MaterialBuffer) m_MaterialBuffer.reset();
    if (m_InstanceBuffer) m_InstanceBuffer.reset();
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        ClearResourceFreeQueue(i);
}

void ResourceManager::InitGlobalResources()
{
    m_MaterialBuffer = std::make_unique<Buffer>(
        sizeof(GpuMaterial) * 1024,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU, "Global_MaterialBuffer");
    m_InstanceBuffer = std::make_unique<Buffer>(
        sizeof(GpuInstance) * 4096,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, "Global_InstanceBuffer");
    CreateTextureSampler();
    CreateDescriptorPool();
    CreateTransientDescriptorPools();
    CreateSceneDescriptorSetLayout();
    AllocatePersistentSets();
    CreateDefaultResources();
}

void ResourceManager::ResetTransientDescriptorPool()
{
    VkDescriptorPool p = m_TransientDescriptorPools[m_CurrentFrameIndex];
    if (p != VK_NULL_HANDLE)
        vkResetDescriptorPool(m_Context->GetDevice(), p, 0);
}
void ResourceManager::CreateTransientDescriptorPools()
{
    m_TransientDescriptorPools.resize(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorPoolSize> s = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4000},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100}};
    VkDescriptorPoolCreateInfo i{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        nullptr,
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        4000,
        (uint32_t)s.size(),
        s.data()};
    for (uint32_t j = 0; j < MAX_FRAMES_IN_FLIGHT; ++j)
        vkCreateDescriptorPool(m_Context->GetDevice(), &i, nullptr,
                               &m_TransientDescriptorPools[j]);
}

void ResourceManager::CreateTextureSampler()
{
    {
        VkSamplerCreateInfo i{
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            nullptr,
            0,
            VK_FILTER_LINEAR,
            VK_FILTER_LINEAR,
            VK_SAMPLER_MIPMAP_MODE_LINEAR,
            VK_SAMPLER_ADDRESS_MODE_REPEAT,
            VK_SAMPLER_ADDRESS_MODE_REPEAT,
            VK_SAMPLER_ADDRESS_MODE_REPEAT,
            0.0f,
            VK_TRUE,
            m_Context->GetDeviceProperties().limits.maxSamplerAnisotropy,
            VK_FALSE,
            VK_COMPARE_OP_ALWAYS,
            0.0f,
            10.0f,
            VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            VK_FALSE};
        vkCreateSampler(m_Context->GetDevice(), &i, nullptr, &m_TextureSampler);
    }

    {
        VkSamplerCreateInfo i{
            VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            nullptr,
            0,
            VK_FILTER_NEAREST,
            VK_FILTER_NEAREST,
            VK_SAMPLER_MIPMAP_MODE_NEAREST,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            VK_COMPARE_OP_ALWAYS,
            0.0f,
            1.0f,
            VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            VK_FALSE};
        vkCreateSampler(m_Context->GetDevice(), &i, nullptr, &m_NearestSampler);
    }
}

void ResourceManager::CreateDescriptorPool()
{
    std::vector<VkDescriptorPoolSize> s = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100}};
    VkDescriptorPoolCreateInfo i{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        nullptr,
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
            VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        1000,
        (uint32_t)s.size(),
        s.data()};
    vkCreateDescriptorPool(m_Context->GetDevice(), &i, nullptr,
                           &m_DescriptorPool);
}

void ResourceManager::CreateSceneDescriptorSetLayout()
{
    std::vector<VkDescriptorSetLayoutBinding> b = {
        {BINDING_AS, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
         VK_SHADER_STAGE_ALL, nullptr},
        {BINDING_MATERIALS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_ALL, nullptr},
        {BINDING_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_ALL, nullptr},
        {BINDING_TEXTURES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024,
         VK_SHADER_STAGE_ALL, nullptr},
        {BINDING_LIGHTS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_ALL, nullptr},
        {BINDING_LIGHTS_CDF, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_ALL, nullptr}};
    VkDescriptorBindingFlags f[6] = {
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT};
    VkDescriptorSetLayoutBindingFlagsCreateInfo lf{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        nullptr, 6, f};
    VkDescriptorSetLayoutCreateInfo li{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &lf,
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        (uint32_t)b.size(), b.data()};
    vkCreateDescriptorSetLayout(m_Context->GetDevice(), &li, nullptr,
                                &m_SceneDescriptorSetLayout);
}

void ResourceManager::AllocatePersistentSets()
{
    VkDescriptorSetLayout eL = m_Context->GetEmptyDescriptorSetLayout();
    VkDescriptorSetAllocateInfo eA{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
        m_DescriptorPool, 1, &eL};
    vkAllocateDescriptorSets(m_Context->GetDevice(), &eA,
                             &m_Context->GetEmptyDescriptorSetRef());
    m_SceneDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    std::vector<VkDescriptorSetLayout> ls(MAX_FRAMES_IN_FLIGHT,
                                          m_SceneDescriptorSetLayout);
    VkDescriptorSetAllocateInfo aI{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
        m_DescriptorPool, MAX_FRAMES_IN_FLIGHT, ls.data()};
    vkAllocateDescriptorSets(m_Context->GetDevice(), &aI,
                             m_SceneDescriptorSets.data());
}

void ResourceManager::CreateDefaultResources()
{
    auto f = std::make_unique<Image>(
        1, 1, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL, "Texture_Default");
    uint8_t m[] = {0, 0, 0, 255};
    Buffer s(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY,
             "Staging_DefaultTexture");
    s.Update(m, 4);
    {
        ScopedCommandBuffer c;
        VulkanUtils::TransitionImageLayout(
            c, (VkImage)f->GetImage(), VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
        VkBufferImageCopy r{0,         0,
                            0,         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                            {0, 0, 0}, {1, 1, 1}};
        vkCmdCopyBufferToImage(c, (VkBuffer)s.GetBuffer(),
                               (VkImage)f->GetImage(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        VulkanUtils::TransitionImageLayout(
            c, (VkImage)f->GetImage(), VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    }
    AddTexture(std::move(f), "Default");
    CreateMaterial("Default");
    SyncMaterialsToGPU();
}

void ResourceManager::UpdateGlobalResources(uint32_t cF,
                                            const UniformBufferObject& ubo)
{
    m_CurrentFrameIndex = cF;
    UniformBufferObject& currentUbo = const_cast<UniformBufferObject&>(ubo);
    currentUbo.envData.y = (float)m_LightManager.GetLightCount();
}

void ResourceManager::UpdateSceneDescriptorSet(Scene* s, uint32_t fI)
{
    if (!s || m_SceneDescriptorSets.empty()) return;
    
    // If fI is 0xFFFFFFFF, we update ALL frames to ensure consistency after major scene changes
    uint32_t start = (fI == 0xFFFFFFFF) ? 0 : fI;
    uint32_t end = (fI == 0xFFFFFFFF) ? (uint32_t)m_SceneDescriptorSets.size() : fI + 1;
    
    for (uint32_t i = start; i < end; ++i)
    {
        VkDescriptorSet tS = m_SceneDescriptorSets[i];
        if (tS == VK_NULL_HANDLE) continue;
        std::vector<VkWriteDescriptorSet> wS;
        VkAccelerationStructureKHR tlas = s->GetTLAS();
        VkWriteDescriptorSetAccelerationStructureKHR aW{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
            nullptr, 1, &tlas};
        if (tlas != VK_NULL_HANDLE)
        {
            VkWriteDescriptorSet w{
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                &aW,
                tS,
                BINDING_AS,
                0,
                1,
                VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR};
            wS.push_back(w);
        }
        static VkDescriptorBufferInfo mI, iI, lI, cI;
        if (m_MaterialBuffer && m_MaterialBuffer->GetBuffer() != VK_NULL_HANDLE)
        {
            mI = {(VkBuffer)m_MaterialBuffer->GetBuffer(), 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet mW{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    nullptr,
                                    tS,
                                    BINDING_MATERIALS,
                                    0,
                                    1,
                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    nullptr,
                                    &mI};
            wS.push_back(mW);
        }
        if (m_InstanceBuffer && m_InstanceBuffer->GetBuffer() != VK_NULL_HANDLE)
        {
            iI = {(VkBuffer)m_InstanceBuffer->GetBuffer(), 0, VK_WHOLE_SIZE};
            VkWriteDescriptorSet iW{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    nullptr,
                                    tS,
                                    BINDING_INSTANCES,
                                    0,
                                    1,
                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    nullptr,
                                    &iI};
            wS.push_back(iW);
        }
        if (m_LightManager.GetLightBuffer())
        {
            lI = {(VkBuffer)m_LightManager.GetLightBuffer()->GetBuffer(), 0,
                  VK_WHOLE_SIZE};
            wS.push_back({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, tS,
                          BINDING_LIGHTS, 0, 1,
                          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &lI});
        }
        if (m_LightManager.GetCDFBuffer())
        {
            cI = {(VkBuffer)m_LightManager.GetCDFBuffer()->GetBuffer(), 0,
                  VK_WHOLE_SIZE};
            wS.push_back({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, tS,
                          BINDING_LIGHTS_CDF, 0, 1,
                          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &cI});
        }
        std::vector<VkDescriptorImageInfo> iIs;
        for (uint32_t j = 0; j < (uint32_t)m_Textures.size(); ++j)
            if (m_Textures[j] &&
                m_Textures[j]->GetImageView() != VK_NULL_HANDLE)
                iIs.push_back({m_TextureSampler, m_Textures[j]->GetImageView(),
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        if (!iIs.empty())
        {
            VkWriteDescriptorSet tW{
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                nullptr,
                tS,
                BINDING_TEXTURES,
                0,
                (uint32_t)std::min((size_t)1024, iIs.size()),
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                iIs.data()};
            wS.push_back(tW);
        }
        if (!wS.empty())
            vkUpdateDescriptorSets(m_Context->GetDevice(), (uint32_t)wS.size(),
                                   wS.data(), 0, nullptr);
    }
}

std::shared_ptr<Model> ResourceManager::LoadModelAsync(
    const std::string& path, std::shared_ptr<Scene> targetScene)
{
    if (m_LoadingModels.count(path)) return m_LoadingModels[path].model;
    auto model = std::make_shared<Model>(m_Context);
    auto future = Application::Get().GetTaskSystem()->Enqueue(
        [path, model]() -> std::shared_ptr<ImportedScene>
        {
            auto sceneData = AssetImporter::ImportScene(path);
            if (sceneData) model->UploadToGPU(*sceneData);
            return sceneData;
        });
    m_LoadingModels.emplace(
        path, LoadingTask{model, std::move(future), targetScene, path});
    return model;
}

void ResourceManager::UpdateLoadingTasks()
{
    for (auto it = m_LoadingModels.begin(); it != m_LoadingModels.end();)
    {
        auto& task = it->second;
        if (task.future.wait_for(std::chrono::seconds(0)) ==
            std::future_status::ready)
        {
            auto sceneData = task.future.get();
            if (sceneData && task.model->IsReady())
                if (task.targetScene)
                    task.targetScene->FinalizeAsyncModelLoad(
                        task.model, sceneData, task.path);
            it = m_LoadingModels.erase(it);
        }
        else
            ++it;
    }
}

void ResourceManager::SyncInstancesToGPU(Scene* scene)
{
    if (!scene || !m_InstanceBuffer) return;
    const auto& entities = scene->GetEntities();
    if (entities.empty()) return;
    std::vector<GpuInstance> instanceData;
    uint32_t instanceIdx = 0;
    for (const auto& entity : entities)
    {
        auto model = entity.mesh.model;
        if (!model || !model->IsReady())
        {
            if (model) instanceIdx += (uint32_t)model->GetMeshes().size();
            continue;
        }
        const auto& meshes = model->GetMeshes();
        glm::mat4 modelMatrix = entity.transform.GetTransform();
        for (const auto& mesh : meshes)
        {
            GpuInstance gpuInst{};
            gpuInst.transform = modelMatrix * mesh.transform;
            gpuInst.inverseTransform = glm::inverse(gpuInst.transform);
            gpuInst.normalTransform = glm::transpose(gpuInst.inverseTransform);
            gpuInst.bounds = mesh.localBounds.ToGpuAABB();
            gpuInst.shape = 0;
            gpuInst.index = instanceIdx++;
            gpuInst.material = (uint)mesh.materialIndex;
            gpuInst.selected = 0;
            gpuInst.vertexAddress =
                model->GetVertexBuffer()->GetDeviceAddress() +
                (mesh.vertexOffset * sizeof(GpuVertex));
            gpuInst.indexAddress = model->GetIndexBuffer()->GetDeviceAddress() +
                                   (mesh.indexOffset * sizeof(uint32_t));
            gpuInst.prevTransform = entity.prevTransform * mesh.transform;
            instanceData.push_back(gpuInst);
        }
    }
    if (instanceData.empty()) return;
    VkDeviceSize dataSize = instanceData.size() * sizeof(GpuInstance);
    if (m_InstanceBuffer->GetSize() < dataSize)
        m_InstanceBuffer = std::make_unique<Buffer>(
            dataSize * 2,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY, "Global_InstanceBuffer_Resized");
    Buffer staging(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VMA_MEMORY_USAGE_CPU_ONLY, "Staging_SyncInstances");
    staging.UploadData(instanceData.data(), dataSize);
    {
        ScopedCommandBuffer cmd;
        VkBufferCopy copy{0, 0, dataSize};
        vkCmdCopyBuffer(cmd, (VkBuffer)staging.GetBuffer(),
                        (VkBuffer)m_InstanceBuffer->GetBuffer(), 1, &copy);
    }
    m_LightManager.Build(scene);
}

void ResourceManager::UpdateMaterial(uint32_t materialIndex,
                                     const GpuMaterial& material)
{
    if (materialIndex < m_Materials.size())
    {
        m_Materials[materialIndex]->SetData(material);
        if (m_MaterialBuffer)
            m_MaterialBuffer->Update(&material, sizeof(GpuMaterial),
                                     materialIndex * sizeof(GpuMaterial));
    }
}
void ResourceManager::SyncMaterialsToGPU()
{
    if (m_Materials.empty()) return;
    VkDeviceSize bufferSize =
        std::max((VkDeviceSize)1024,
                 (VkDeviceSize)(sizeof(GpuMaterial) * m_Materials.size()));
    if (!m_MaterialBuffer || m_MaterialBuffer->GetSize() < bufferSize)
        m_MaterialBuffer = std::make_unique<Buffer>(
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU, "Global_MaterialBuffer_Resized");
    std::vector<GpuMaterial> materialData;
    for (const auto& mat : m_Materials)
        materialData.push_back(mat ? mat->GetData() : GpuMaterial{});
    Buffer staging(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VMA_MEMORY_USAGE_CPU_ONLY, "Staging_SyncMaterials");
    staging.UploadData(materialData.data(),
                       sizeof(GpuMaterial) * materialData.size());
    {
        ScopedCommandBuffer cmd;
        VkBufferCopy copy{0, 0, sizeof(GpuMaterial) * materialData.size()};
        vkCmdCopyBuffer(cmd, (VkBuffer)staging.GetBuffer(),
                        (VkBuffer)m_MaterialBuffer->GetBuffer(), 1, &copy);
    }
}

GraphImage ResourceManager::CreateGraphImage(uint32_t w, uint32_t h, VkFormat f,
                                             VkImageUsageFlags u,
                                             VkImageLayout iL,
                                             VkSampleCountFlagBits s,
                                             const std::string& name)
{
    GraphImage i{};
    i.width = w;
    i.height = h;
    i.format = f;
    bool isDepth = VulkanUtils::IsDepthFormat(f);
    VkImageUsageFlags finalUsage = u | VK_IMAGE_USAGE_SAMPLED_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    i.usage = finalUsage;
    VkImageCreateInfo iI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                         nullptr,
                         0,
                         VK_IMAGE_TYPE_2D,
                         f,
                         {w, h, 1},
                         1,
                         1,
                         s,
                         VK_IMAGE_TILING_OPTIMAL,
                         finalUsage,
                         VK_SHARING_MODE_EXCLUSIVE,
                         0,
                         nullptr,
                         VK_IMAGE_LAYOUT_UNDEFINED};
    VmaAllocationCreateInfo vA{0, VMA_MEMORY_USAGE_GPU_ONLY};
    vmaCreateImage(m_Context->GetAllocator(), &iI, &vA, &i.handle,
                   &i.allocation, nullptr);
    if (!name.empty())
        m_Context->SetDebugName((uint64_t)i.handle, VK_OBJECT_TYPE_IMAGE,
                                name.c_str());
    VkImageViewCreateInfo vW{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        i.handle,
        VK_IMAGE_VIEW_TYPE_2D,
        f,
        {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        {(VkImageAspectFlags)(isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT
                                      : VK_IMAGE_ASPECT_COLOR_BIT),
         0, 1, 0, 1}};
    vkCreateImageView(m_Context->GetDevice(), &vW, nullptr, &i.view);
    if (isDepth)
    {
        vW.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R,
                         VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE};
        vkCreateImageView(m_Context->GetDevice(), &vW, nullptr, &i.debug_view);
    }
    else
        i.debug_view = i.view;
    return i;
}

void ResourceManager::DestroyGraphImage(GraphImage& i)
{
    if (i.handle == VK_NULL_HANDLE) return;
    VkDevice device = m_Context->GetDevice();
    std::set<VkImageView> uniqueViews;
    if (i.view != VK_NULL_HANDLE) uniqueViews.insert(i.view);
    if (i.debug_view != VK_NULL_HANDLE) uniqueViews.insert(i.debug_view);
    for (auto v : uniqueViews) vkDestroyImageView(device, v, nullptr);
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
    {
        std::lock_guard<std::mutex> lock(m_AssetMutex);
        if (m_TextureMap.count(p)) return m_TextureMap[p];
    }
    int tw, th, tc;
    unsigned char* px = stbi_load(p.c_str(), &tw, &th, &tc, 4);
    if (!px) return TextureHandle();
    VkDeviceSize s = (VkDeviceSize)tw * th * 4;
    Buffer st(s, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
              "Staging_LoadTexture");
    st.Update(px, s);
    stbi_image_free(px);
    VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    auto im = std::make_unique<Image>(
        (uint32_t)tw, (uint32_t)th, format,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL, "Texture_" + p);
    {
        ScopedCommandBuffer c;
        VulkanUtils::TransitionImageLayout(
            c, (VkImage)im->GetImage(), format, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
        VkBufferImageCopy r{0,         0,
                            0,         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                            {0, 0, 0}, {(uint32_t)tw, (uint32_t)th, 1}};
        vkCmdCopyBufferToImage(c, (VkBuffer)st.GetBuffer(),
                               (VkImage)im->GetImage(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        VulkanUtils::TransitionImageLayout(
            c, (VkImage)im->GetImage(), format,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    }
    return AddTexture(std::move(im), p);
}

TextureHandle ResourceManager::LoadHDRTexture(const std::string& p)
{
    {
        std::lock_guard<std::mutex> lock(m_AssetMutex);
        if (m_TextureMap.count(p)) return m_TextureMap[p];
    }
    int tw, th, tc;
    float* px = stbi_loadf(p.c_str(), &tw, &th, &tc, 4);
    if (!px) return TextureHandle();
    VkDeviceSize s = (VkDeviceSize)tw * th * 4 * sizeof(float);
    Buffer st(s, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
              "Staging_LoadHDRTexture");
    st.UploadData(px, s);
    stbi_image_free(px);
    auto im = std::make_unique<Image>(
        (uint32_t)tw, (uint32_t)th, VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL, "Texture_HDR_" + p);
    {
        ScopedCommandBuffer c;
        VulkanUtils::TransitionImageLayout(
            c, (VkImage)im->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
        VkBufferImageCopy r{0,         0,
                            0,         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                            {0, 0, 0}, {(uint32_t)tw, (uint32_t)th, 1}};
        vkCmdCopyBufferToImage(c, (VkBuffer)st.GetBuffer(),
                               (VkImage)im->GetImage(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        VulkanUtils::TransitionImageLayout(
            c, (VkImage)im->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    }
    return AddTexture(std::move(im), p);
}

void ResourceManager::LoadHDR(const std::string& path)
{
    Application::Get().QueueEvent(
        [this, path]()
        {
            if (!m_Context) return;

            // Ensure GPU is idle before heavy allocation and texture upload
            vkDeviceWaitIdle(m_Context->GetDevice());

            TextureHandle handle;
            std::string ext = std::filesystem::path(path).extension().string();

            CH_CORE_INFO("ResourceManager: Loading HDR from {0}", path);

            if (ext == ".hdr" || ext == ".exr")
                handle = LoadHDRTexture(path);
            else
                handle = LoadTexture(path, false);

            if (!handle.IsValid())
            {
                CH_CORE_ERROR("ResourceManager: Failed to load HDR texture {0}",
                              path);
                return;
            }

            if (m_ActiveScene)
            {
                m_ActiveScene->SetSkyboxTextureIndex(handle.id);
                m_ActiveScene->MarkMaterialDirty();
                // Force update all descriptor sets to ensure the new texture is
                // visible
                UpdateSceneDescriptorSet(m_ActiveScene.get(), 0xFFFFFFFF);
            }

            if (auto* renderPath = Application::Get().GetActiveRenderPath())
            {
                renderPath->OnSceneUpdated();
            }
        });
}

void ResourceManager::LoadScene(const std::string& path)
{
    Application::Get().QueueEvent(
        [this, path]()
        {
            vkDeviceWaitIdle(m_Context->GetDevice());
            if (m_ActiveScene)
            {
                m_ActiveScene->LoadModel(path);
            }
        });
}

void ResourceManager::ClearScene()
{
    Application::Get().QueueEvent(
        [this]()
        {
            vkDeviceWaitIdle(m_Context->GetDevice());
            ClearRuntimeAssets();
            auto newScene = std::make_shared<Scene>(m_Context);
            SetActiveScene(newScene);
        });
}

TextureHandle ResourceManager::AddTexture(std::unique_ptr<Image> t,
                                          const std::string& n)
{
    std::lock_guard<std::mutex> lock(m_AssetMutex);
    uint32_t idx = (uint32_t)m_Textures.size();
    m_Textures.push_back(std::move(t));
    m_TextureRefCount.push_back(1);
    if (!n.empty()) m_TextureMap[n] = TextureHandle(idx);
    return TextureHandle(idx);
}
MaterialHandle ResourceManager::CreateMaterial(const std::string& n)
{
    return AddMaterial(std::make_unique<Material>(n), n);
}
MaterialHandle ResourceManager::AddMaterial(std::unique_ptr<Material> m,
                                            const std::string& n)
{
    std::lock_guard<std::mutex> lock(m_AssetMutex);
    uint32_t idx = (uint32_t)m_Materials.size();
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
    if (h.id < m_TextureRefCount.size() && --m_TextureRefCount[h.id] == 0 &&
        h.id != 0)
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
        m_Buffers[h.id].reset();
}
void ResourceManager::AddRef(MaterialHandle h)
{
    if (h.id < m_MaterialRefCount.size()) m_MaterialRefCount[h.id]++;
}
void ResourceManager::Release(MaterialHandle h)
{
    if (h.id < m_MaterialRefCount.size() && --m_MaterialRefCount[h.id] == 0 &&
        h.id != 0)
    {
        if (m_Materials[h.id]) m_Materials[h.id]->SetData(GpuMaterial{});
    }
}
void ResourceManager::SubmitResourceFree(std::function<void()>&& f)
{
    if (s_Instance)
        s_Instance->m_ResourceFreeQueue[s_Instance->m_CurrentFrameIndex]
            .push_back(std::move(f));
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
    if (h.id < m_Textures.size() && m_Textures[h.id])
        return m_Textures[h.id].get();
    return m_Textures.empty() ? nullptr : m_Textures[0].get();
}
Material* ResourceManager::GetMaterial(MaterialHandle h)
{
    if (h.id < m_Materials.size() && m_Materials[h.id])
        return m_Materials[h.id].get();
    return m_Materials.empty() ? nullptr : m_Materials[0].get();
}
Buffer* ResourceManager::GetBuffer(BufferHandle h)
{
    return (h.id < m_Buffers.size() && m_Buffers[h.id]) ? m_Buffers[h.id].get()
                                                        : nullptr;
}
TextureHandle ResourceManager::GetTextureIndex(const std::string& n)
{
    return m_TextureMap.count(n) ? m_TextureMap[n] : TextureHandle();
}
void AddRefInternal(Handle<Image> h)
{
    ResourceManager::Get().AddRef(h);
}
void ReleaseInternal(Handle<Image> h)
{
    ResourceManager::Get().Release(h);
}
void AddRefInternal(Handle<Buffer> h)
{
    ResourceManager::Get().AddRef(h);
}
void ReleaseInternal(Handle<Buffer> h)
{
    ResourceManager::Get().Release(h);
}
void AddRefInternal(Handle<Material> h)
{
    ResourceManager::Get().AddRef(h);
}
void ReleaseInternal(Handle<Material> h)
{
    ResourceManager::Get().Release(h);
}

TextureHandle ResourceManager::GenerateBlueNoise(uint32_t width,
                                                 uint32_t height)
{
    std::vector<uint8_t> data(width * height * 4);
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> dist(0, 255);

    for (uint32_t i = 0; i < width * height; ++i)
    {
        data[i * 4 + 0] = dist(rng);
        data[i * 4 + 1] = dist(rng);
        data[i * 4 + 2] = dist(rng);
        data[i * 4 + 3] = 255;
    }

    auto im = std::make_unique<Image>(
        width, height, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL, "ProceduralBlueNoise");

    VkDeviceSize s = width * height * 4;
    Buffer st(s, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
              "Staging_BlueNoise");
    st.Update(data.data(), s);

    {
        ScopedCommandBuffer c;
        VulkanUtils::TransitionImageLayout(
            c, (VkImage)im->GetImage(), VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1);
        VkBufferImageCopy r{0,         0,
                            0,         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                            {0, 0, 0}, {width, height, 1}};
        vkCmdCopyBufferToImage(c, (VkBuffer)st.GetBuffer(),
                               (VkImage)im->GetImage(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        VulkanUtils::TransitionImageLayout(
            c, (VkImage)im->GetImage(), VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    }

    return AddTexture(std::move(im), "BlueNoise");
}
} // namespace Chimera
