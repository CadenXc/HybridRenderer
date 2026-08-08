#include "Renderer/Graph/RenderGraph.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void TestEmptyGraphCompilesAndExecutesSafely()
{
    Chimera::RenderGraph graph(1280, 720);

    graph.Compile();

    VkSemaphore result = graph.Execute(VK_NULL_HANDLE);

    Require(result == VK_NULL_HANDLE, "empty graph should not submit GPU work");

    Require(graph.GetParallelLayers().empty(),
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
        [](EmptyPassData&, Chimera::RenderGraph::PassBuilder& builder)
        { builder.Read("MissingInput"); },
        [](const EmptyPassData&, Chimera::RenderGraphRegistry&,
           VkCommandBuffer) {});

    bool rejected = false;

    try
    {
        graph.Compile();
    }
    catch (const std::logic_error& e)
    {
        const std::string message = e.what();

        Require(message.find("InvalidReadPass") != std::string::npos,
                "diagnostic does not contain the pass name");

        Require(message.find("MissingInput") != std::string::npos,
                "diagnostic does not contain the resource name");

        rejected = true;
    }

    Require(rejected, "Compile accepted a read of an undeclared resource");
}

struct WritePassData
{
    Chimera::RGResourceHandle output;
};

void AddWriter(Chimera::RenderGraph& graph, const std::string& passName)
{
    graph.AddPassRaw<WritePassData>(
        passName,
        [](WritePassData& data, Chimera::RenderGraph::PassBuilder& builder)
        {
            data.output =
                builder.Write("SharedImage").Format(VK_FORMAT_R8G8B8A8_UNORM);
        },
        [](const WritePassData&, Chimera::RenderGraphRegistry&,
           VkCommandBuffer) {});
}

struct ReadPassData
{
    Chimera::RGResourceHandle input;
};

void AddReader(Chimera::RenderGraph& graph, const std::string& passName)
{
    graph.AddPassRaw<ReadPassData>(
        passName,
        [](ReadPassData& data, Chimera::RenderGraph::PassBuilder& builder)
        { data.input = builder.Read("SharedImage"); },
        [](const ReadPassData&, Chimera::RenderGraphRegistry&,
           VkCommandBuffer) {});
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

    const auto& layers = graph.GetParallelLayers();

    Require(layers.size() == 2,
            "two writers of the same resource must have a WAW dependency");

    Require(layers[0].size() == 1 && layers[0][0] == 0,
            "WriterA should be the only pass in layer 0");

    Require(layers[1].size() == 1 && layers[1][0] == 1,
            "WriterB should be the only pass in layer 1");
}

void TestSameStateWriteRequiresBarrier()
{
    Chimera::ResourceState writeState{
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};

    Require(Chimera::RequiresImageMemoryBarrier(writeState, writeState),
            "same-state write-after-write must require a barrier");
}

void TestSameStateReadDoesNotRequireBarrier()
{
    Chimera::ResourceState readState{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     VK_ACCESS_2_SHADER_READ_BIT,
                                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT};

    Require(!Chimera::RequiresImageMemoryBarrier(readState, readState),
            "same-state read-after-read should not require a barrier");
}

void TestTopologicalLayersIgnorePassIndexOrder()
{
    const std::vector<std::vector<uint32_t>> dependencies{
        {1}, // pass 0 waits for pass 1
        {2}, // pass 1 waits for pass 2
        {}   // pass 2 has no predecessor
    };

    const auto layers =
        Chimera::RenderGraph::BuildExecutionLayers(dependencies);

    Require(layers.size() == 3,
            "transitive dependency chain should produce three layers");

    Require(layers[0] == std::vector<uint32_t>{2},
            "pass 2 should execute first");

    Require(layers[1] == std::vector<uint32_t>{1},
            "pass 1 should execute second");

    Require(layers[2] == std::vector<uint32_t>{0},
            "pass 0 should execute last");
}

void TestDependencyCycleIsRejected()
{
    bool rejected = false;

    try
    {
        Chimera::RenderGraph::BuildExecutionLayers({
            {1}, // pass 0 waits for pass 1
            {0}  // pass 1 waits for pass 0
        });
    }
    catch (const std::logic_error& error)
    {
        rejected = std::string(error.what()).find("cycle") != std::string::npos;
    }

    Require(rejected, "dependency cycle must be rejected");
}

void TestEmptyDependenciesProduceNoLayers()
{
    const std::vector<std::vector<uint32_t>> dependencies;

    const auto layers =
        Chimera::RenderGraph::BuildExecutionLayers(dependencies);

    Require(layers.empty(), "empty dependency graph should produce no layers");
}

void TestInvalidPredecessorIsRejected()
{
    bool rejected = false;

    try
    {
        Chimera::RenderGraph::BuildExecutionLayers({{1}});
    }
    catch (const std::logic_error& error)
    {
        rejected =
            std::string(error.what()).find("invalid pass") != std::string::npos;
    }

    Require(rejected, "invalid predecessor index must be rejected");
}

void TestDuplicateDependencyIsCountedOnce()
{
    const std::vector<std::vector<uint32_t>> dependencies{{}, {0, 0}};

    const auto layers =
        Chimera::RenderGraph::BuildExecutionLayers(dependencies);

    Require(layers.size() == 2,
            "duplicate dependency should not create extra layers");

    Require(layers[0] == std::vector<uint32_t>{0},
            "pass 0 should execute first");

    Require(layers[1] == std::vector<uint32_t>{1},
            "duplicate edge must be counted once");
}

void TestSelfDependencyIsRejected()
{
    bool rejected = false;

    try
    {
        Chimera::RenderGraph::BuildExecutionLayers({{0}});
    }
    catch (const std::logic_error& error)
    {
        rejected = std::string(error.what()).find("cycle") != std::string::npos;
    }

    Require(rejected, "self dependency must be rejected as a cycle");
}

} // namespace

int main()
{
    try
    {
        TestEmptyGraphCompilesAndExecutesSafely();
        std::cout << "[PASS] empty graph compiles and executes safely\n";

        TestInvalidReadIsRejected();
        std::cout << "[PASS] invalid resource read is rejected\n";

        TestWritersArePlacedInSeparateLayers();
        std::cout << "[PASS] same-resource writers are serialized\n";

        TestSameStateWriteRequiresBarrier();
        std::cout << "[PASS] same-state WAW requires a barrier\n";

        TestSameStateReadDoesNotRequireBarrier();
        std::cout << "[PASS] same-state read-read skips the barrier\n";

        TestWriterWaitsForEarlierReader();
        std::cout << "[PASS] writers wait for earlier readers\n";

        TestTopologicalLayersIgnorePassIndexOrder();
        std::cout << "[PASS] topological layers ignore pass index order\n";

        TestDependencyCycleIsRejected();
        std::cout << "[PASS] dependency cycles are rejected\n";

        TestEmptyDependenciesProduceNoLayers();
        std::cout << "[PASS] empty dependency graph produces no layers\n";

        TestInvalidPredecessorIsRejected();
        std::cout << "[PASS] invalid predecessors are rejected\n";

        TestDuplicateDependencyIsCountedOnce();
        std::cout << "[PASS] duplicate dependencies are counted once\n";

        TestSelfDependencyIsRejected();
        std::cout << "[PASS] self dependencies are rejected\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}