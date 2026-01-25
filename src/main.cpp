#include "pch.h"
#include "core/EntryPoint.h"

// ==============================================================================
// 客户端应用
// ==============================================================================
class ChimeraApp : public Chimera::Application
{
public:
    ChimeraApp()
    {
        CH_INFO("---------------------------------------------");
        CH_INFO("Welcome to Chimera Hybrid Renderer!");
        CH_INFO("App constructed successfully.");
        CH_INFO("---------------------------------------------");
    }

    ~ChimeraApp()
    {
        CH_INFO("Chimera App shutting down...");
    }
};

Chimera::Application* Chimera::CreateApplication(int argc, char** argv)
{
    return new ChimeraApp();
}