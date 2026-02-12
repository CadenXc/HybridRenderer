#include "pch.h"
#include "RenderState.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/Buffer.h"

namespace Chimera
{
    RenderState::RenderState()
    {
        CreateDescriptorSetLayout();
        CreateResources();
        CreateDescriptorSets();
    }

    RenderState::~RenderState()
    {
        VkDevice device = VulkanContext::Get().GetDevice();
        vkDeviceWaitIdle(device);

        // [FIX] Explicitly clear frame buffers first
        m_Frames.clear();

        vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayout, nullptr);
        vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
    }

    void RenderState::Update(uint32_t frameIndex, const UniformBufferObject& data)
    {
        m_Frames[frameIndex].UBO->Update(&data, sizeof(UniformBufferObject));
    }

    void RenderState::CreateDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr };
        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &uboLayoutBinding };
        
        if (vkCreateDescriptorSetLayout(VulkanContext::Get().GetDevice(), &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("RenderState: failed to create descriptor set layout!");
        }
    }

    void RenderState::CreateResources()
    {
        m_Frames.resize(MAX_FRAMES_IN_FLIGHT);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            m_Frames[i].UBO = std::make_unique<Buffer>(
                sizeof(UniformBufferObject),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );
            CH_CORE_TRACE("RenderState: Allocated UBO[{0}] at {1}", i, (void*)m_Frames[i].UBO->GetBuffer());
        }
    }

    void RenderState::CreateDescriptorSets()
    {
        CH_CORE_INFO("RenderState: Creating Descriptor Sets...");
        // Use a dedicated pool for managing Global Sets
        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, MAX_FRAMES_IN_FLIGHT, 1, &poolSize };
        
        if (vkCreateDescriptorPool(VulkanContext::Get().GetDevice(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("RenderState: failed to create descriptor pool!");
        }

        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_DescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_DescriptorPool, MAX_FRAMES_IN_FLIGHT, layouts.data() };
        
        m_DescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        VkResult res = vkAllocateDescriptorSets(VulkanContext::Get().GetDevice(), &allocInfo, m_DescriptorSets.data());
        if (res != VK_SUCCESS)
        {
            CH_CORE_ERROR("RenderState: Failed to allocate descriptor sets! Result: {0}", (int)res);
            throw std::runtime_error("RenderState: failed to allocate descriptor sets!");
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (m_DescriptorSets[i] == VK_NULL_HANDLE)
            {
                CH_CORE_ERROR("RenderState: Descriptor set {0} is NULL after allocation!", i);
            }
            VulkanContext::Get().SetDebugName((uint64_t)m_DescriptorSets[i], VK_OBJECT_TYPE_DESCRIPTOR_SET, ("Set0_Global_Frame_" + std::to_string(i)).c_str());
            
            VkDescriptorBufferInfo bufferInfo{ (VkBuffer)(void*)(uintptr_t)m_Frames[i].UBO->GetBuffer(), 0, sizeof(UniformBufferObject) };
            VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_DescriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bufferInfo, nullptr };
            vkUpdateDescriptorSets(VulkanContext::Get().GetDevice(), 1, &descriptorWrite, 0, nullptr);
        }
        CH_CORE_INFO("RenderState: Descriptor Sets Updated.");
    }

}