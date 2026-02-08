#pragma once
#include "volk.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

// VMA Forward Declarations (Avoid complete include here)
typedef struct VmaAllocation_T* VmaAllocation;

namespace Chimera {

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
    enum class RenderPathType { Forward, Hybrid, RayTracing };
    enum class TransientResourceType { Image, Buffer, Sampler, AccelerationStructure, Storage };
    enum class TransientImageType { AttachmentImage, SampledImage, StorageImage };
    
    struct ApplicationSpecification { 
        std::string Name = "Chimera App"; 
        uint32_t Width = 1600; 
        uint32_t Height = 900; 
    };

}