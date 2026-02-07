#include "Chimera.h"
#include "Core/EntryPoint.h"
#include "Core/EngineConfig.h"
#include "editor/EditorLayer.h"

class ChimeraApp : public Chimera::Application
{
public:
	ChimeraApp(const Chimera::ApplicationSpecification& spec)
		: Chimera::Application(spec)
	{
		// Push the Editor Layer which contains the UI and logic
		PushLayer(std::make_shared<Chimera::EditorLayer>(this));

        // Load default model and skybox
        LoadScene(Chimera::Config::ASSET_DIR + "models/fantasy_queen/scene.gltf");
        LoadSkybox(Chimera::Config::ASSET_DIR + "textures/newport_loft.hdr");
		
		CH_INFO("---------------------------------------------");
		CH_INFO("Welcome to Chimera Hybrid Renderer!");
		CH_INFO("App constructed successfully.");
		CH_INFO("---------------------------------------------");
	}

    void ExecuteLoadScene(const std::string& path)
    {
        CH_CORE_INFO("SandboxApp: Loading default model.");
        Application::ExecuteLoadScene(path);
        auto scene = GetScene();
        if (scene && !scene->GetEntities().empty()) {
            scene->UpdateEntityTRS(0, {0,0,0}, {0,0,0}, {1,1,1});
        }
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
	// If a scene path is provided on the command line, request loading it on startup
	if (argc > 1 && argv[1])
	{
		try
		{
			app->LoadScene(std::string(argv[1]));
		}
		catch (...)
		{
			// Ignore; Application will handle failures when executing the load
		}
	}

	return app;
}
