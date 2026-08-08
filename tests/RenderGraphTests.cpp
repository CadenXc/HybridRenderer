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

struct WritePassData
{
    Chimera::RGResourceHandle output;
};

void AddWriter(
    Chimera::RenderGraph& graph,
    const std::string& passName)
{
    graph.AddPassRaw<WritePassData>(
        passName,
        [](WritePassData& data,
           Chimera::RenderGraph::PassBuilder& builder)
        {
            data.output =
                builder.Write("SharedImage")
                    .Format(VK_FORMAT_R8G8B8A8_UNORM);
        },
        [](const WritePassData&,
           Chimera::RenderGraphRegistry&,
           VkCommandBuffer)
        {
        });
}

struct ReadPassData
{
    Chimera::RGResourceHandle input;
};

void AddReader(
    Chimera::RenderGraph& graph,
    const std::string& passName)
{
    graph.AddPassRaw<ReadPassData>(
        passName,
        [](ReadPassData& data,
           Chimera::RenderGraph::PassBuilder& builder)
        {
            data.input = builder.Read("SharedImage");
        },
        [](const ReadPassData&,
           Chimera::RenderGraphRegistry&,
           VkCommandBuffer)
        {
        });
}

void TestWriterWaitsForEarlierReader()
{
    Chimera::RenderGraph graph(1280, 720);

    AddWriter(graph, "WriterA");
    AddReader(graph, "ReaderA");
    AddReader(graph, "ReaderB");
    AddWriter(graph, "WriterB");

    graph.Compile();

    const auto& layers = graph.GetParallelLayers();

    Require(layers.size() == 3, "a writer must wait for all earlier readers");

    Require(layers[0].size() == 1 && layers[0][0] == 0,
            "WriterA should be the only pass in layer 0");

    Require(layers[1].size() == 2 && layers[1][0] == 1 && layers[1][1] == 2,
            "independent readers should share layer 1");

    Require(layers[2].size() == 1 && layers[2][0] == 3,
            "WriterB should wait in layer 2");

    const auto& dependencies = graph.GetPassDependencies();

    Require(dependencies.size() == 4,
            "dependency table must contain one entry per pass");

    Require(dependencies[0].empty(), "WriterA should have no predecessors");

    Require(dependencies[1] == std::vector<uint32_t>{0},
            "ReaderA should depend on WriterA");

    Require(dependencies[2] == std::vector<uint32_t>{0},
            "ReaderB should depend on WriterA");

    Require(dependencies[3] == std::vector<uint32_t>{0, 1, 2},
            "WriterB should depend on WriterA and both readers");
}

void TestWritersArePlacedInSeparateLayers()
{
    Chimera::RenderGraph graph(1280, 720);

    AddWriter(graph, "WriterA");
    AddWriter(graph, "WriterB");

    graph.Compile();

    const auto& layers =
        graph.GetParallelLayers();

    Require(
        layers.size() == 2,
        "two writers of the same resource must have a WAW dependency");

    Require(
        layers[0].size() == 1 &&
        layers[0][0] == 0,
        "WriterA should be the only pass in layer 0");

    Require(
        layers[1].size() == 1 &&
        layers[1][0] == 1,
        "WriterB should be the only pass in layer 1");
}

void TestSameStateWriteRequiresBarrier()
{
    Chimera::ResourceState writeState{
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    Require(
        Chimera::RequiresImageMemoryBarrier(
            writeState,
            writeState),
        "same-state write-after-write must require a barrier");
}

void TestSameStateReadDoesNotRequireBarrier()
{
    Chimera::ResourceState readState{
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_2_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
    };

    Require(
        !Chimera::RequiresImageMemoryBarrier(
            readState,
            readState),
        "same-state read-after-read should not require a barrier");
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

        TestWritersArePlacedInSeparateLayers();
        std::cout << "[PASS] same-resource writers are serialized\n";

        TestSameStateWriteRequiresBarrier();
        std::cout << "[PASS] same-state WAW requires a barrier\n";

        TestSameStateReadDoesNotRequireBarrier();
        std::cout << "[PASS] same-state read-read skips the barrier\n";

        TestWriterWaitsForEarlierReader();
        std::cout << "[PASS] writers wait for earlier readers\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}