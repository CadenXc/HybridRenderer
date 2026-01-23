#pragma once

#include "Core/Application.h"
#include "Core/Log.h"
#include "Core/Random.h"

extern Chimera::Application* Chimera::CreateApplication(int argc, char** argv);

int main(int argc, char** argv)
{
    Chimera::Log::Init();
    Chimera::Random::Init();
    CH_CORE_INFO("Chimera Engine Initialized (via EntryPoint)");

    auto app = Chimera::CreateApplication(argc, argv);

    app->Run();

    delete app;

    return 0;
}