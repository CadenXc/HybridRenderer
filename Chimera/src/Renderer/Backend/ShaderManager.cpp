#include "pch.h"
#include "ShaderManager.h"
#include "Core/Log.h"
#include "Core/EngineConfig.h"
#include <fstream>

namespace Chimera {

    void ShaderManager::Init(const std::string& shaderDir, const std::string& sourceDir) {
        s_ShaderDir = shaderDir;
        s_SourceDir = sourceDir;
    }

    std::vector<uint32_t> ShaderManager::GetShaderCode(const std::string& name) {
        std::string filename = name;
        if (filename.find(".spv") == std::string::npos) filename += ".spv";

        std::filesystem::path fullpath = std::filesystem::absolute(s_ShaderDir + filename);
        if (!std::filesystem::exists(fullpath)) {
            // Try relative fallback
            fullpath = std::filesystem::path(s_ShaderDir) / std::filesystem::path(filename).filename();
        }

        if (std::filesystem::exists(fullpath)) {
            s_Timestamps[fullpath.string()] = std::filesystem::last_write_time(fullpath);
        } else {
            CH_CORE_ERROR("ShaderManager: Cannot find shader file: {0}", fullpath.string());
            throw std::runtime_error("shader file not found");
        }

        return LoadSPV(fullpath.string());
    }

    bool ShaderManager::CheckForUpdates() {
        bool changed = false;
        
        // 1. Check SPV files
        for (auto& [pathStr, lastTime] : s_Timestamps) {
            if (std::filesystem::exists(pathStr)) {
                auto currentTime = std::filesystem::last_write_time(pathStr);
                if (currentTime != lastTime) {
                    CH_CORE_INFO("ShaderManager: Change detected in compiled shader: {0}", std::filesystem::path(pathStr).filename().string());
                    changed = true;
                    // Note: We don't update lastTime here, PipelineManager will clear cache and re-load
                }
            }
        }

        // 2. Check source files if directory exists
        if (std::filesystem::exists(s_SourceDir)) {
            for (auto const& entry : std::filesystem::recursive_directory_iterator(s_SourceDir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".rgen" || ext == ".rchit" || ext == ".rmiss") {
                        auto lastTime = std::filesystem::last_write_time(entry.path());
                        std::string pathStr = entry.path().string();
                        
                        if (s_Timestamps.count(pathStr) && s_Timestamps[pathStr] != lastTime) {
                            CH_CORE_INFO("ShaderManager: Source change detected: {0}. Auto-recompiling...", entry.path().filename().string());
                            RecompileAll();
                            s_Timestamps[pathStr] = lastTime;
                            return true; // Recompile will trigger SPV update check in next frames
                        }
                        s_Timestamps[pathStr] = lastTime;
                    }
                }
            }
        }

        return changed;
    }

    void ShaderManager::RecompileAll() {
        CH_CORE_INFO("ShaderManager: Running CompileShaders script...");
        // Use relative path to scripts from root
        std::system("powershell.exe -ExecutionPolicy Bypass -File ../../../scripts/CompileShaders.ps1");
    }

    std::vector<uint32_t> ShaderManager::LoadSPV(const std::string& path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("failed to open spv file: " + path);
        size_t fileSize = (size_t)file.tellg();
        std::vector<uint32_t> buffer(fileSize / 4);
        file.seekg(0);
        file.read((char*)buffer.data(), fileSize);
        file.close();
        return buffer;
    }

}
