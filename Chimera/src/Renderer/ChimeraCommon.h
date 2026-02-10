#pragma once
#include "volk.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

// VMA Forward Declarations (Avoid complete include here)
typedef struct VmaAllocation_T* VmaAllocation;

namespace Chimera
{
    // 1. 核心前置声明
    class VulkanContext;
    class ResourceManager;
    class PipelineManager;
    class RenderGraph;
    class Scene;
    class RenderState;
    struct GraphicsPipeline;
    struct RaytracingPipeline;
    struct ComputePipeline;

    // 2. 基础枚举与常量
    inline static const uint32_t MAX_FRAMES_IN_FLIGHT = 3;
    
    enum class RenderPathType 
    { 
        Forward, 
        Hybrid, 
        RayTracing 
    };

    inline const char* RenderPathTypeToString(RenderPathType type)
    {
        switch (type)
        {
            case RenderPathType::Forward:    return "Forward";
            case RenderPathType::Hybrid:     return "Hybrid";
            case RenderPathType::RayTracing:  return "Ray Tracing";
            default:                        return "Unknown";
        }
    }

    inline const std::vector<RenderPathType>& GetAllRenderPathTypes()
    {
        static std::vector<RenderPathType> all = { RenderPathType::Forward, RenderPathType::Hybrid, RenderPathType::RayTracing };
        return all;
    }

    enum class TransientResourceType { Image, Buffer, Sampler, AccelerationStructure, Storage };
    enum class TransientImageType { AttachmentImage, SampledImage, StorageImage };
    
    struct RenderFrameInfo
    {
        VkCommandBuffer commandBuffer;
        uint32_t frameIndex;
        uint32_t imageIndex;
        VkDescriptorSet globalSet;
    };

    struct ApplicationSpecification
    { 
        std::string Name = "Chimera App"; 
        uint32_t Width = 1600; 
        uint32_t Height = 900; 
    };
}
