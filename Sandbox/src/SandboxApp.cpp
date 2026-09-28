#include "Chimera.h"
#include "Core/EntryPoint.h"
#include "automation/EditorAutomationController.h"
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
        else if (std::string_view(argv[argumentIndex]) ==
                 "--object-motion-smoke")
        {
            automationOptions.objectMotionSmokeTest = true;
        }
        else if (std::string_view(argv[argumentIndex]) ==
                 "--render-path-smoke")
        {
            automationOptions.renderPathSmokeTest = true;
        }
        else if (std::string_view(argv[argumentIndex]) ==
                 "--svgf-spatial-only-smoke")
        {
            automationOptions.renderPathSmokeTest = true;
            automationOptions.svgfSmokeMode = SvgfSmokeMode::SpatialOnly;
        }
        else if (std::string_view(argv[argumentIndex]) ==
                 "--svgf-temporal-only-smoke")
        {
            automationOptions.renderPathSmokeTest = true;
            automationOptions.svgfSmokeMode = SvgfSmokeMode::TemporalOnly;
        }
        else if (std::string_view(argv[argumentIndex]) ==
                 "--svgf-temporal-spatial-smoke")
        {
            automationOptions.renderPathSmokeTest = true;
            automationOptions.svgfSmokeMode = SvgfSmokeMode::TemporalAndSpatial;
        }
        else if (std::string_view(argv[argumentIndex]) ==
                 "--svgf-toggle-smoke")
        {
            automationOptions.renderPathSmokeTest = true;
            automationOptions.svgfToggleSmokeTest = true;
            automationOptions.svgfSmokeMode = SvgfSmokeMode::TemporalAndSpatial;
        }
        else if (std::string_view(argv[argumentIndex]) ==
                 "--render-path-texture-smoke")
        {
            automationOptions.renderPathSmokeTest = true;
            automationOptions.texturedSceneSmokeTest = true;
            automationOptions.svgfSmokeMode = SvgfSmokeMode::TemporalAndSpatial;
        }
        else if (std::string_view(argv[argumentIndex]) ==
                 "--hybrid-multi-object-smoke")
        {
            automationOptions.hybridMultiObjectSmokeTest = true;
            automationOptions.svgfSmokeMode = SvgfSmokeMode::TemporalAndSpatial;
        }
        else if (std::string_view(argv[argumentIndex]) ==
                 "--hybrid-benchmark-smoke")
        {
            automationOptions.hybridBenchmarkSmokeTest = true;
        }
    }

    if (automationOptions.svgfToggleSmokeTest)
        automationOptions.svgfSmokeMode = SvgfSmokeMode::TemporalAndSpatial;

    ChimeraApp* app = new ChimeraApp(spec, automationOptions);

    return app;
}
