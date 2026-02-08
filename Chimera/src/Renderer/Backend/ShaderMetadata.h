#pragma once
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace Chimera {

    struct ShaderResourceBinding {
        std::string name;
        uint32_t binding;
        VkDescriptorType type;
        uint32_t count = 1;
        VkShaderStageFlags stage;
    };

    // 存储一个 Shader 程序（或一组 RT Shader）的布局信息
    struct ShaderLayout {
        std::string name;
        std::unordered_map<std::string, ShaderResourceBinding> resources; // By Name
        std::unordered_map<uint32_t, ShaderResourceBinding> bindings;   // By Binding Point
        
        bool HasResource(const std::string& name) const { return resources.find(name) != resources.end(); }
        const ShaderResourceBinding& GetResource(const std::string& name) const { return resources.at(name); }

        void Merge(const ShaderLayout& other) {
            for (auto& [name, res] : other.resources) {
                if (resources.count(name)) {
                    resources[name].stage |= res.stage;
                } else {
                    resources[name] = res;
                }
            }
            for (auto& [b, res] : other.bindings) {
                if (bindings.count(b)) {
                    bindings[b].stage |= res.stage;
                } else {
                    bindings[b] = res;
                }
            }
        }
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

        static const ShaderLayout& GetMergedLayout(const std::string& name, const std::vector<std::string>& shaderNames);

    private:
        inline static std::unordered_map<std::string, ShaderLayout> s_Layouts;
    };

}
