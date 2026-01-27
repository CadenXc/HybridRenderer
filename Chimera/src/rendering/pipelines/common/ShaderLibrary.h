#pragma once

#include "pch.h"

namespace Chimera {

    class ShaderLibrary {
    public:
        /**
         * @brief 加载并创建一个 VkShaderModule。
         * 自动处理路径拼接 (.spv) 和文件读取。
         */
        static VkShaderModule LoadShader(VkDevice device, const std::string& name);

        /**
         * @brief 辅助函数：根据文件名和阶段创建 VkPipelineShaderStageCreateInfo。
         */
        static VkPipelineShaderStageCreateInfo CreateShaderStage(VkDevice device, 
                                                                const std::string& name, 
                                                                VkShaderStageFlagBits stage);
    };

}
