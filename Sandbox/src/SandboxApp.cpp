#include "Chimera.h"
#include "Core/EntryPoint.h"
#include "editor/EditorLayer.h"

class ChimeraApp : public Chimera::Application
{
public:
    ChimeraApp(const Chimera::ApplicationSpecification& spec)
        : Chimera::Application(spec)
    {
        auto editorLayer = std::make_shared<Chimera::EditorLayer>();
        PushLayer(editorLayer);

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

    ChimeraApp* app = new ChimeraApp(spec);

    return app;
}
