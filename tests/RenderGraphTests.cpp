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

struct EmptyPassData
{
};

void TestInvalidReadIsRejected()
{
    Chimera::RenderGraph graph(1280, 720);

    graph.AddPassRaw<EmptyPassData>(
        "InvalidReadPass",
        [](EmptyPassData&,
           Chimera::RenderGraph::PassBuilder& builder)
        {
            builder.Read("MissingInput");
        },
        [](const EmptyPassData&,
           Chimera::RenderGraphRegistry&,
           VkCommandBuffer)
        {
        });

    bool rejected = false;

    try
    {
        graph.Compile();
    }
    catch (const std::logic_error& e)
    {
        const std::string message = e.what();

        Require(
            message.find("InvalidReadPass") != std::string::npos,
            "diagnostic does not contain the pass name");

        Require(
            message.find("MissingInput") != std::string::npos,
            "diagnostic does not contain the resource name");

        rejected = true;
    }

    Require(
        rejected,
        "Compile accepted a read of an undeclared resource");
}
}

int main()
{
    try
    {
        TestEmptyGraphCompilesAndExecutesSafely();
        std::cout
            << "[PASS] empty graph compiles and executes safely\n";

        TestInvalidReadIsRejected();
        std::cout
            << "[PASS] invalid resource read is rejected\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}