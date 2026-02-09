#pragma once
#include "pch.h"
#include "Renderer/ChimeraCommon.h"
#include "Scene/SceneCommon.h"
#include "Renderer/Resources/ResourceHandle.h"

#include "Renderer/Backend/ShaderCommon.h"

namespace Chimera
{
    class RenderState
    {
    public:
        RenderState();
        ~RenderState();

        // 核心 API: 每帧开始时收集数据并上传 GPU
        void Update(uint32_t frameIndex, const UniformBufferObject& data);

        VkDescriptorSet GetDescriptorSet(uint32_t frameIndex) const
        {
            return m_DescriptorSets[frameIndex];
        }

        VkDescriptorSetLayout GetLayout() const
        {
            return m_DescriptorSetLayout;
        }

    private:
        void CreateDescriptorSetLayout();
        void CreateResources();
        void CreateDescriptorSets();

    private:
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;
        
        struct FrameResources
        {
            std::unique_ptr<class Buffer> UBO;
        };
        std::vector<FrameResources> m_Frames;
    };
}