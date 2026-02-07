#pragma once
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace Chimera {

    struct ShaderResourceBinding {
        uint32_t binding;
        VkDescriptorType type;
        uint32_t count = 1;
        VkShaderStageFlags stage;
    };

    // 存储一个 Shader 程序（或一组 RT Shader）的布局信息
    struct ShaderLayout {
        std::string name;
        std::unordered_map<std::string, ShaderResourceBinding> resources;
        
        bool HasResource(const std::string& name) const { return resources.find(name) != resources.end(); }
        const ShaderResourceBinding& GetResource(const std::string& name) const { return resources.at(name); }
    };

    // 全局 Shader 布局管理器
    class ShaderLibrary {
    public:
        static void RegisterLayout(const std::string& name, const ShaderLayout& layout) {
            s_Layouts[name] = layout;
        }

        static const ShaderLayout& GetLayout(const std::string& name) {
            return s_Layouts.at(name);
        }

    private:
        inline static std::unordered_map<std::string, ShaderLayout> s_Layouts;
    };

}
