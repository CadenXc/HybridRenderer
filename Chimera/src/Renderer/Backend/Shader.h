#pragma once
#include "pch.h"
#include "ShaderMetadata.h"

namespace Chimera {

    class Shader {
    public:
        Shader(const std::string& path);
        ~Shader();

        const std::vector<uint32_t>& GetBytecode() const { return m_Bytecode; }
        const ShaderLayout& GetLayout() const { return m_Layout; }
        uint32_t GetPushConstantSize() const { return m_PushConstantSize; }

    private:
        void Reflect();

    private:
        std::string m_Path;
        std::vector<uint32_t> m_Bytecode;
        ShaderLayout m_Layout;
        uint32_t m_PushConstantSize = 0;
    };

}
