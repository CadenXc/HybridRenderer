#pragma once
#include "pch.h"
#include <variant>
#include <vector>
#include <array>
#include <string>
#include <functional>
#include <memory>

namespace Chimera {

	class Buffer;

	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

	struct ImageDescription
	{
		uint32_t width;
		uint32_t height;
		VkFormat format;
		VkImageUsageFlags usage;
		VkSampleCountFlagBits samples;

		bool operator==(const ImageDescription& other) const
		{
			return width == other.width && height == other.height &&
				   format == other.format && usage == other.usage &&
				   samples == other.samples;
		}
	};

	struct BufferDescription
	{
		VkDeviceSize size;
		VkBufferUsageFlags usage;
		VmaMemoryUsage memory_usage;

		bool operator==(const BufferDescription& other) const
		{
			return size == other.size && usage == other.usage && memory_usage == other.memory_usage;
		}
	};

	struct GraphImage
	{
		VkImage handle;
		VkImageView view;
		VmaAllocation allocation;
		uint32_t width;
		uint32_t height;
		VkFormat format;
		VkImageUsageFlags usage;
		bool is_external = false;
	};

	enum class VertexInputState { Default, Empty, ImGui };
	enum class PrimitiveTopology { TriangleList, LineList };
	enum class CullMode { None, Front, Back };
	enum class FrontFace { Clockwise, CounterClockwise };
	enum class DepthCompare {
		Less = VK_COMPARE_OP_LESS,
		LessOrEqual = VK_COMPARE_OP_LESS_OR_EQUAL,
		Greater = VK_COMPARE_OP_GREATER,
		GreaterOrEqual = VK_COMPARE_OP_GREATER_OR_EQUAL,
		Equal = VK_COMPARE_OP_EQUAL,
		Always = VK_COMPARE_OP_ALWAYS
	};

	struct RasterizationDescription
	{
		PrimitiveTopology Topology = PrimitiveTopology::TriangleList;
		CullMode Cull = CullMode::Back;
		FrontFace Front = FrontFace::CounterClockwise;
		float LineWidth = 1.0f;
	};

	struct DepthStencilDescription
	{
		bool DepthTest = true;
		bool DepthWrite = true;
		DepthCompare DepthCompare = DepthCompare::LessOrEqual;
	};

	struct MultisampleDescription { VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT; };
	struct BlendDescription { bool Enabled = false; };
	enum class DynamicState { None, Viewport, ViewportScissor, DepthBias };

	struct PushConstantDescription { VkShaderStageFlags shader_stage; uint32_t size; };
	static const PushConstantDescription PUSHCONSTANTS_NONE = { 0, 0 };

	struct SpecializationConstantsDescription { VkShaderStageFlags shader_stage; std::vector<int> specialization_constants; };

	enum class TransientResourceType { Image, Buffer, AccelerationStructure, Sampler, Storage };
	enum class TransientImageType { AttachmentImage, SampledImage, StorageImage };

	struct TransientImage
	{
		TransientImageType type;
		uint32_t width;
		uint32_t height;
		VkFormat format;
		uint32_t binding;
		VkClearValue clear_value;
		bool multisampled;
		VkDescriptorType descriptor_type_override = VK_DESCRIPTOR_TYPE_MAX_ENUM;
	};

	struct TransientBuffer
	{
		uint32_t stride;
		uint32_t count;
		uint32_t binding;
		VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		VkBuffer handle = VK_NULL_HANDLE;
	};

	struct TransientAccelerationStructure { uint32_t binding; VkAccelerationStructureKHR handle = VK_NULL_HANDLE; };

	struct TransientResource
	{
		TransientResourceType type;
		std::string name;
		union
		{
			TransientImage image;
			TransientBuffer buffer;
			TransientAccelerationStructure as;
		};

		TransientResource() : type(TransientResourceType::Image), name("") { memset(&image, 0, sizeof(image)); image.descriptor_type_override = VK_DESCRIPTOR_TYPE_MAX_ENUM; }
		TransientResource(TransientResourceType t, const std::string& n) : type(t), name(n) { memset(&image, 0, sizeof(image)); image.descriptor_type_override = VK_DESCRIPTOR_TYPE_MAX_ENUM; }

		static TransientResource Image(const std::string& name, VkFormat format, uint32_t binding = 0xFFFFFFFF, VkClearValue clear = { {0.0f, 0.0f, 0.0f, 0.0f} }, TransientImageType type = TransientImageType::AttachmentImage)
		{
			TransientResource res(TransientResourceType::Image, name);
			res.image.type = type; res.image.format = format; res.image.binding = binding; res.image.clear_value = clear;
			res.image.width = 0; res.image.height = 0; res.image.multisampled = false;
			return res;
		}

		static TransientResource Sampler(const std::string& name, uint32_t binding = 0xFFFFFFFF, uint32_t count = 1)
		{
			TransientResource res(TransientResourceType::Sampler, name);
			res.buffer.binding = binding; res.buffer.count = count; res.buffer.descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			return res;
		}

		static TransientResource StorageBuffer(const std::string& name, uint32_t binding = 0xFFFFFFFF, uint32_t count = 1)
		{
			TransientResource res(TransientResourceType::Storage, name);
			res.buffer.binding = binding; res.buffer.count = count; res.buffer.descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			return res;
		}

		static TransientResource Buffer(const std::string& name, VkBuffer handle = VK_NULL_HANDLE)
		{
			TransientResource res(TransientResourceType::Buffer, name);
			res.buffer.binding = 0xFFFFFFFF; res.buffer.handle = handle; res.buffer.count = 1; res.buffer.descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			return res;
		}

        static TransientResource Buffer(const std::string& name, uint32_t binding, VkBuffer handle = VK_NULL_HANDLE)
		{
			TransientResource res(TransientResourceType::Buffer, name);
			res.buffer.binding = binding; res.buffer.handle = handle; res.buffer.count = 1; res.buffer.descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			return res;
		}

        static TransientResource AccelerationStructure(const std::string& name, VkAccelerationStructureKHR handle = VK_NULL_HANDLE)
        {
            TransientResource res(TransientResourceType::AccelerationStructure, name);
            res.as.binding = 0xFFFFFFFF; res.as.handle = handle;
            return res;
        }
	};

	struct GraphicsPipelineDescription
	{
		std::string name;
		std::string vertex_shader;
		std::string fragment_shader;
		VertexInputState vertex_input_state = VertexInputState::Default;
		RasterizationDescription Rasterization;
		MultisampleDescription Multisample;
		DepthStencilDescription DepthStencil;
		BlendDescription Blend;
		DynamicState dynamic_state = DynamicState::None;
		PushConstantDescription push_constants = PUSHCONSTANTS_NONE;
		SpecializationConstantsDescription specialization_constants_description;
	};

	struct GraphicsPipeline { GraphicsPipelineDescription description; VkPipeline handle; VkPipelineLayout layout; };

	struct HitShader { std::string closest_hit; std::string any_hit; };

	struct RaytracingPipelineDescription
	{
		std::string name;
		std::string raygen_shader;
		std::vector<std::string> miss_shaders;
		std::vector<HitShader> hit_shaders;
		PushConstantDescription push_constants;
	};
	
	struct ShaderBindingTable { VkStridedDeviceAddressRegionKHR strided_device_address_region; };

	struct RaytracingPipeline
	{
		RaytracingPipelineDescription description;
		uint32_t shader_group_size;
		ShaderBindingTable raygen_sbt; ShaderBindingTable miss_sbt; ShaderBindingTable hit_sbt; ShaderBindingTable call_sbt;
		std::shared_ptr<class Buffer> sbt_buffer;
		VkPipeline handle; VkPipelineLayout layout;
	};

	struct ComputeKernel { std::string shader; };
	struct ComputePipelineDescription { std::vector<ComputeKernel> kernels; PushConstantDescription push_constant_description; };
	struct ComputePipeline { VkPipeline handle; VkPipelineLayout layout; PushConstantDescription push_constant_description; };

    // --- Forward Declarations for Callbacks ---
    class GraphicsExecutionContext;
	class RaytracingExecutionContext;
	class ComputeExecutionContext;

	using GraphicsExecutionCallback = std::function<void(GraphicsExecutionContext &)>;
	using ExecuteGraphicsCallback = std::function<void(std::string, GraphicsExecutionCallback)>;
	using GraphicsPassCallback = std::function<void(ExecuteGraphicsCallback)>;

	using RaytracingExecutionCallback = std::function<void(RaytracingExecutionContext &)>;
	using ExecuteRaytracingCallback = std::function<void(std::string, RaytracingExecutionCallback)>;
	using RaytracingPassCallback = std::function<void(ExecuteRaytracingCallback)>;

	using ComputePassCallback = std::function<void(ComputeExecutionContext &)>;

    // --- Specifications (Moved after callback definitions) ---
	struct GraphicsPassSpecification
	{
		std::string Name;
		std::vector<TransientResource> Dependencies;
		std::vector<TransientResource> Outputs;
		std::vector<GraphicsPipelineDescription> Pipelines;
		GraphicsPassCallback Callback;
		std::string ShaderLayout = "";
	};

	struct RaytracingPassSpecification
	{
		std::string Name;
		std::vector<TransientResource> Dependencies;
		std::vector<TransientResource> Outputs;
		RaytracingPipelineDescription Pipeline;
		RaytracingPassCallback Callback;
		std::string ShaderLayout = "";
	};

    struct ComputePassSpecification
    {
        std::string Name;
        std::vector<TransientResource> Dependencies;
        std::vector<TransientResource> Outputs;
        ComputePipelineDescription Pipeline;
        ComputePassCallback Callback;
        std::string ShaderLayout = "";
    };

	struct GraphicsPass
	{
		VkRenderPass handle;
		std::vector<TransientResource> attachments;
		std::vector<VkFramebuffer> framebuffers;
		GraphicsPassCallback callback;
	};

	enum class ResourceUsage { ColorAttachment, DepthAttachment, ShaderRead, StorageRead, StorageWrite, TransferSrc, TransferDst, Present };

	struct ImageAccess { VkImageLayout layout; VkAccessFlags access_flags; VkPipelineStageFlags stage_flags; };

	struct RaytracingPass { RaytracingPassCallback callback; };
	struct ComputePass { ComputePassCallback callback; };
	struct BlitPass { std::string srcName; std::string dstName; };

	struct RenderPass
	{
		std::string name;
		VkDescriptorSetLayout descriptor_set_layout;
		VkDescriptorSet descriptor_set;
		std::variant<GraphicsPass, RaytracingPass, ComputePass, BlitPass> pass;
	};

	struct ComputePassDescription { ComputePipelineDescription pipeline_description; ComputePassCallback callback; };
	struct GraphicsPassDescription { std::vector<GraphicsPipelineDescription> pipeline_descriptions; GraphicsPassCallback callback; };
	struct RaytracingPassDescription { RaytracingPipelineDescription pipeline_description; RaytracingPassCallback callback; };
	struct BlitPassDescription {};

	struct RenderPassDescription
	{
		std::string name;
		std::vector<TransientResource> dependencies;
		std::vector<TransientResource> outputs;
		std::variant<GraphicsPassDescription, RaytracingPassDescription, ComputePassDescription, BlitPassDescription> description;
	};

}