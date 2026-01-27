#include "Chimera.h"
#include "core/application/EntryPoint.h"

class ChimeraApp : public Chimera::Application
{
public:
    ChimeraApp(const Chimera::ApplicationSpecification& spec)
        : Chimera::Application(spec)
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
    Chimera::ApplicationSpecification spec;
    spec.Name = "Chimera Hybrid Renderer";
    spec.Width = 1600;
    spec.Height = 900;

    return new ChimeraApp(spec);
}
