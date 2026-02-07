#pragma once

#include "Renderer/Graph/RenderGraphPass.h"

namespace Chimera {

    class SVGFAtrousPass : public RenderGraphPass {
    public:
        SVGFAtrousPass(const std::string& name, const std::string& inputName, const std::string& outputName, int stepSize);
        virtual void Setup(RenderGraph& graph) override;

    private:
        std::string m_InputName;
        std::string m_OutputName;
        int m_StepSize;
    };

}
