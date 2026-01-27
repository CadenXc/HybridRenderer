#include "pch.h"
#include "rendering/pipelines/common/ShaderLibrary.h"
#include "core/Config.h"
#include "core/utilities/FileIO.h"

namespace Chimera {

    VkShaderModule ShaderLibrary::LoadShader(VkDevice device, const std::string& name)
    {
        std::string filename = name;
        if (filename.find(".spv") == std::string::npos) {
            filename += ".spv";
        }

        std::string fullPath = std::string(Config::SHADER_DIR) + filename;
        auto code = FileIO::ReadFile(fullPath);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("failed to create shader module from: " + fullPath);
        }

        return shaderModule;
    }

    VkPipelineShaderStageCreateInfo ShaderLibrary::CreateShaderStage(VkDevice device, 
                                                                    const std::string& name, 
                                                                    VkShaderStageFlagBits stage)
    {
        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = stage;
        stageInfo.module = LoadShader(device, name);
        stageInfo.pName = "main";
        return stageInfo;
    }

}

