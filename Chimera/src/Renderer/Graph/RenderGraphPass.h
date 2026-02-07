#pragma once

#include "pch.h"
#include "RenderGraph.h"

namespace Chimera {

    class RenderGraphPass
    {
    public:
        RenderGraphPass(const std::string& name) : m_Name(name) {}
        virtual ~RenderGraphPass() = default;

        virtual void Setup(RenderGraph& graph) = 0;
        
        const std::string& GetName() const { return m_Name; }

    protected:
        // Helper methods to create common resources with less boilerplate
        TransientResource InputImage(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED) {
            return TransientResource::Image(name, format, 0xFFFFFFFF, {0}, TransientImageType::SampledImage);
        }

        TransientResource InputStorage(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED) {
            return TransientResource::Image(name, format, 0xFFFFFFFF, {0}, TransientImageType::StorageImage);
        }

        TransientResource OutputAttachment(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED) {
            return TransientResource::Image(name, format, 0xFFFFFFFF, {0}, TransientImageType::AttachmentImage);
        }

        TransientResource OutputStorage(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED) {
            return TransientResource::Image(name, format, 0xFFFFFFFF, {0}, TransientImageType::StorageImage);
        }

        TransientResource InputBuffer(const std::string& name, VkBuffer handle) {
            return TransientResource::Buffer(name, handle);
        }

    protected:
        std::string m_Name;
    };

}
