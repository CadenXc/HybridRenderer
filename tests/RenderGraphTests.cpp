#include "Renderer/Graph/RenderGraph.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void TestEmptyGraphCompilesAndExecutesSafely()
{
    Chimera::RenderGraph graph(1280, 720);

    graph.Compile();

    VkSemaphore result = graph.Execute(VK_NULL_HANDLE);

    Require(
        result == VK_NULL_HANDLE,
        "empty graph should not submit GPU work");

    Require(
        graph.GetParallelLayers().empty(),
        "empty graph should not contain execution layers");
}
}

int main()
{
    try
    {
        TestEmptyGraphCompilesAndExecutesSafely();
        std::cout << "[PASS] empty graph compiles and executes safely\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "[FAIL] empty graph compiles and executes safely: "
            << e.what() << '\n';
        return 1;
    }
}