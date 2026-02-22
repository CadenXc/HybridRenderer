#pragma once
#include "pch.h"
#include "Scene/Scene.h" 
#include "Renderer/ChimeraCommon.h"

namespace Chimera
{

	// Note: Not constexpr because we access static methods of Vertex which might not be constexpr
	// or just to keep it simple with C++ linkage.

	inline const VkVertexInputBindingDescription DEFAULT_VERTEX_BINDING_DESCRIPTION = Vertex::getBindingDescription();
	inline const std::array<VkVertexInputAttributeDescription, 4> DEFAULT_VERTEX_ATTRIBUTE_DESCRIPTIONS = Vertex::getAttributeDescriptions();

	// ImGui Vertex Definitions (Placeholder if needed, otherwise rely on ImGui impl)
	struct ImGuiVertex {
		glm::vec2 pos;
		glm::vec2 uv;
		uint32_t col;
	};
	inline const VkVertexInputBindingDescription IMGUI_VERTEX_BINDING_DESCRIPTION {
		.binding = 0,
		.stride = sizeof(ImGuiVertex),
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
	};
	inline const std::array<VkVertexInputAttributeDescription, 3> IMGUI_VERTEX_ATTRIBUTE_DESCRIPTIONS {
		VkVertexInputAttributeDescription {
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(ImGuiVertex, pos)
		},
		VkVertexInputAttributeDescription {
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(ImGuiVertex, uv)
		},
		VkVertexInputAttributeDescription {
			.location = 2,
			.binding = 0,
			.format = VK_FORMAT_R8G8B8A8_UNORM,
			.offset = offsetof(ImGuiVertex, col)
		}
	};

	inline const VkPipelineVertexInputStateCreateInfo VERTEX_INPUT_STATE_DEFAULT {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &DEFAULT_VERTEX_BINDING_DESCRIPTION,
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(DEFAULT_VERTEX_ATTRIBUTE_DESCRIPTIONS.size()),
		.pVertexAttributeDescriptions = DEFAULT_VERTEX_ATTRIBUTE_DESCRIPTIONS.data()
	};
	inline const VkPipelineVertexInputStateCreateInfo VERTEX_INPUT_STATE_IMGUI {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &IMGUI_VERTEX_BINDING_DESCRIPTION,
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(IMGUI_VERTEX_ATTRIBUTE_DESCRIPTIONS.size()),
		.pVertexAttributeDescriptions = IMGUI_VERTEX_ATTRIBUTE_DESCRIPTIONS.data()
	};
	inline const VkPipelineVertexInputStateCreateInfo VERTEX_INPUT_STATE_EMPTY {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 0,
		.pVertexBindingDescriptions = nullptr,
		.vertexAttributeDescriptionCount = 0,
		.pVertexAttributeDescriptions = nullptr
	};
	inline const VkPipelineRasterizationStateCreateInfo RASTERIZATION_STATE_DEFAULT {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f
	};
	inline const VkPipelineRasterizationStateCreateInfo RASTERIZATION_STATE_CULL_CLOCKWISE = RASTERIZATION_STATE_DEFAULT;
	
	inline const VkPipelineRasterizationStateCreateInfo RASTERIZATION_STATE_CULL_COUNTER_CLOCKWISE {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_FRONT_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f
	};

	inline const VkPipelineRasterizationStateCreateInfo RASTERIZATION_STATE_CULL_NONE {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f
	};
	inline const VkPipelineMultisampleStateCreateInfo MULTISAMPLE_STATE_OFF {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};
	inline const VkPipelineDepthStencilStateCreateInfo DEPTH_STENCIL_STATE_ON {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
	};
	inline const VkPipelineDepthStencilStateCreateInfo DEPTH_STENCIL_STATE_OFF {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_FALSE,
		.depthWriteEnable = VK_FALSE
	};
	inline const VkPipelineColorBlendAttachmentState COLOR_BLEND_ATTACHMENT_STATE_OFF {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};
	inline const VkPipelineColorBlendAttachmentState COLOR_BLEND_ATTACHMENT_STATE_IMGUI {
		.blendEnable = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	inline const std::array<VkDynamicState, 2> VIEWPORT_SCISSOR_STATES = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	inline const VkPipelineDynamicStateCreateInfo DYNAMIC_STATE_VIEWPORT_SCISSOR {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = static_cast<uint32_t>(VIEWPORT_SCISSOR_STATES.size()),
		.pDynamicStates = VIEWPORT_SCISSOR_STATES.data()
	};
	inline const std::array<VkDynamicState, 1> DEPTH_BIAS_STATE = {
		VK_DYNAMIC_STATE_DEPTH_BIAS
	};
	inline const VkPipelineDynamicStateCreateInfo DYNAMIC_STATE_DEPTH_BIAS {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = static_cast<uint32_t>(DEPTH_BIAS_STATE.size()),
		.pDynamicStates = DEPTH_BIAS_STATE.data()
	};

}


