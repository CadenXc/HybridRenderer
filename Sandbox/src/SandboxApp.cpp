#include "Chimera.h"
#include "Core/EntryPoint.h"
#include "editor/EditorLayer.h"

#include <string_view>

class ChimeraApp : public Chimera::Application
{
public:
    ChimeraApp(const Chimera::ApplicationSpecification& spec,
               Chimera::EditorAutomationOptions automationOptions)
        : Chimera::Application(spec)
    {
        auto editorLayer =
            std::make_shared<Chimera::EditorLayer>(automationOptions);
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

    EditorAutomationOptions automationOptions;
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        if (std::string_view(argv[argumentIndex]) ==
            "--taa-disocclusion-smoke")
        {
            automationOptions.taaDisocclusionSmokeTest = true;
        }
    }

    ChimeraApp* app = new ChimeraApp(spec, automationOptions);

    return app;
}
