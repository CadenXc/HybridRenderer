#pragma once

#include "Core/Application.h"
#include "Core/Log.h"
#include "Core/Random.h"

#include <filesystem>

extern Chimera::Application* Chimera::CreateApplication(int argc, char** argv);

int main(int argc, char** argv)
{
	Chimera::Log::Init();
	Chimera::Random::Init();
	CH_CORE_INFO("Chimera Engine Initialized (via EntryPoint)");

	// Set working directory to executable directory
	if (argc > 0)
	{
		std::filesystem::path exePath(argv[0]);
		std::filesystem::path exeDir = exePath.parent_path();
		if (std::filesystem::exists(exeDir)) {
			 std::filesystem::current_path(exeDir);
			 CH_CORE_INFO("Set Working Directory to: {}", exeDir.string());
		}
	}

	auto app = Chimera::CreateApplication(argc, argv);

	app->Run();

	delete app;

	return 0;
}
