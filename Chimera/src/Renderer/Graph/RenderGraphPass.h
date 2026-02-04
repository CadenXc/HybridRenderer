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
        std::string m_Name;
    };

}
