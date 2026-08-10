#include "pch.h"
#include "ExecutionContext.h"
#include "Renderer/Graph/RenderGraphCommon.h"
#include "Renderer/Backend/Shader.h"

namespace Chimera
{
ExecutionContext::ExecutionContext(RenderGraph& graph, RenderGraphPass& pass,
                                   VkCommandBuffer cmd)
    : m_Graph(graph), m_Pass(pass), m_Cmd(cmd)
{
}

void ExecutionContext::PushConstants(VkShaderStageFlags stages,
                                     const void* data, uint32_t size)
{
    if (m_ActiveLayout != VK_NULL_HANDLE)
    {
        vkCmdPushConstants(m_Cmd, m_ActiveLayout, stages, 0, size, data);
    }
}

bool ExecutionContext::UsesNamedBindings() const
{
    const auto hasNamedRequest = [](const auto& requests)
    {
        return std::any_of(
            requests.begin(), requests.end(),
            [](const ResourceRequest& request)
            {
                return !request.bindingName.empty();
            });
    };

    return hasNamedRequest(m_Pass.inputs) ||
           hasNamedRequest(m_Pass.outputs);
}

RGResourceHandle ExecutionContext::ResolveNamedImageBinding(
    const ShaderResource& shaderResource) const
{
    const std::vector<ResourceRequest>* requests = nullptr;

    switch (shaderResource.type)
    {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            requests = &m_Pass.inputs;
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            requests = &m_Pass.outputs;
            break;
        default:
        {
            std::ostringstream message;
            message << "Named descriptor resolution error: pass '"
                    << m_Pass.name << "' has unsupported descriptor '"
                    << shaderResource.name << "' at binding "
                    << shaderResource.binding << " with type "
                    << static_cast<uint32_t>(shaderResource.type);
            throw std::logic_error(message.str());
        }
    }

    const ResourceRequest* match = nullptr;

    for (const ResourceRequest& request : *requests)
    {
        if (request.bindingName != shaderResource.name)
        {
            continue;
        }

        if (match != nullptr)
        {
            std::ostringstream message;
            message << "Named descriptor resolution error: pass '"
                    << m_Pass.name << "' declares shader binding '"
                    << shaderResource.name << "' more than once (binding "
                    << shaderResource.binding << ", type "
                    << static_cast<uint32_t>(shaderResource.type) << ")";
            throw std::logic_error(message.str());
        }

        match = &request;
    }

    if (match == nullptr)
    {
        std::ostringstream message;
        message << "Named descriptor resolution error: pass '" << m_Pass.name
                << "' does not declare shader binding '"
                << shaderResource.name << "' (binding "
                << shaderResource.binding << ", type "
                << static_cast<uint32_t>(shaderResource.type) << ")";
        throw std::logic_error(message.str());
    }

    if (match->handle == INVALID_RESOURCE)
    {
        std::ostringstream message;
        message << "Named descriptor resolution error: pass '" << m_Pass.name
                << "' resolves shader binding '" << shaderResource.name
                << "' to an invalid RenderGraph resource (binding "
                << shaderResource.binding << ", type "
                << static_cast<uint32_t>(shaderResource.type) << ")";
        throw std::logic_error(message.str());
    }

    return match->handle;
}
} // namespace Chimera
