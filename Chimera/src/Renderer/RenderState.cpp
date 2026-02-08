#include "pch.h"
#include "RenderState.h"
#include "Renderer/Backend/VulkanContext.h"
#include "Renderer/Resources/Buffer.h"

namespace Chimera {

    RenderState::RenderState(const std::shared_ptr<VulkanContext>& context)
        : m_Context(context)
    {
        CreateDescriptorSetLayout();
        CreateResources();
        CreateDescriptorSets();
    }

    RenderState::~RenderState()
    {
        VkDevice device = m_Context->GetDevice();
        vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayout, nullptr);
        vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
    }

    void RenderState::Update(uint32_t frameIndex, const GlobalFrameData& data)
    {
        m_Frames[frameIndex].UBO->Update(&data, sizeof(GlobalFrameData));
    }

    void RenderState::CreateDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr };
        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &uboLayoutBinding };
        
        if (vkCreateDescriptorSetLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("RenderState: failed to create descriptor set layout!");
        }
    }

    void RenderState::CreateResources()
    {
        m_Frames.resize(MAX_FRAMES_IN_FLIGHT);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            m_Frames[i].UBO = std::make_unique<Buffer>(
                m_Context->GetAllocator(),
                sizeof(GlobalFrameData),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );
        }
    }

    void RenderState::CreateDescriptorSets()
    {
        CH_CORE_INFO("RenderState: Creating Descriptor Sets...");
        // 创建一个专用的池用于管理 Global Sets
        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, MAX_FRAMES_IN_FLIGHT, 1, &poolSize };
        
        if (vkCreateDescriptorPool(m_Context->GetDevice(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("RenderState: failed to create descriptor pool!");
        }

        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_DescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_DescriptorPool, MAX_FRAMES_IN_FLIGHT, layouts.data() };
        
        m_DescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        VkResult res = vkAllocateDescriptorSets(m_Context->GetDevice(), &allocInfo, m_DescriptorSets.data());
        if (res != VK_SUCCESS) {
            CH_CORE_ERROR("RenderState: Failed to allocate descriptor sets! Result: {0}", (int)res);
            throw std::runtime_error("RenderState: failed to allocate descriptor sets!");
        }

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (m_DescriptorSets[i] == VK_NULL_HANDLE) {
                CH_CORE_ERROR("RenderState: Descriptor set {0} is NULL after allocation!", i);
            }
            VkDescriptorBufferInfo bufferInfo{ m_Frames[i].UBO->GetBuffer(), 0, sizeof(GlobalFrameData) };
            VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_DescriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bufferInfo, nullptr };
            vkUpdateDescriptorSets(m_Context->GetDevice(), 1, &descriptorWrite, 0, nullptr);
        }
        CH_CORE_INFO("RenderState: Descriptor Sets Updated.");
    }

}
