#include "Core/Log.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/ExecutionContext.h"
#include "Renderer/Backend/Shader.h"
#include "Renderer/Passes/TAAPass.h"
#include "Renderer/Passes/SVGFPass.h"

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

class TestExecutionContext : public Chimera::ExecutionContext
{
public:
    using ExecutionContext::ExecutionContext;
    using ExecutionContext::ResolveNamedImageBinding;
    using ExecutionContext::UsesNamedBindings;
};

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

void TestTAAFirstFramePreservesHistoryBinding()
{
    Chimera::RenderGraph graph(1280, 720);

    // 建立 TAA 所需的当前帧资源。
    Chimera::RenderGraphPass producerPass;
    Chimera::RenderGraph::PassBuilder producerBuilder(graph, producerPass);

    producerBuilder.Write(Chimera::RS::FinalColor)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);

    producerBuilder.Write(Chimera::RS::Motion)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);

    producerBuilder.Write(Chimera::RS::Depth).Format(VK_FORMAT_D32_SFLOAT);

    // 此时 graph 中还不存在 TAAOutput history。
    Chimera::RenderGraphPass taaGraphPass;
    taaGraphPass.name = "TAAPass";
    taaGraphPass.isCompute = true;

    Chimera::RenderGraph::PassBuilder taaBuilder(graph, taaGraphPass);

    Chimera::TAAPass taaPass;
    Chimera::TAAPassData data{};

    taaPass.Setup(data, taaBuilder);

    Require(taaGraphPass.inputs.size() == 4,
            "TAA first frame must preserve all four input bindings");

    Require(data.history == data.current,
            "TAA first frame should use current color as history fallback");

    Require(taaGraphPass.inputs[0].handle == data.current,
            "TAA binding 0 should contain current color");

    Require(taaGraphPass.inputs[1].handle == data.history,
            "TAA binding 1 should contain history fallback");

    Require(taaGraphPass.inputs[2].handle == data.motion,
            "TAA binding 2 should contain motion");

    Require(taaGraphPass.inputs[3].handle == data.depth,
            "TAA binding 3 should contain depth");

    Require(taaGraphPass.inputs[0].bindingName == "curColor",
            "TAA current color must bind to curColor");

    Require(taaGraphPass.inputs[1].bindingName == "historyColor",
            "TAA history fallback must preserve historyColor binding");

    Require(taaGraphPass.inputs[2].bindingName == "gMotion",
            "TAA motion must bind to gMotion");

    Require(taaGraphPass.inputs[3].bindingName == "gDepth",
            "TAA depth must bind to gDepth");

    Require(taaGraphPass.outputs.size() == 1,
            "TAA must declare exactly one output");

    Require(taaGraphPass.outputs[0].bindingName == "outFinal",
            "TAA output must bind to outFinal");
}

void TestSVGFCombineUsesOnlyShaderInputs()
{
    Chimera::RenderGraph graph(1280, 720);

    const std::string currentName = "TestFiltered";

    Chimera::RenderGraphPass producerPass;
    Chimera::RenderGraph::PassBuilder producerBuilder(graph, producerPass);

    producerBuilder.WriteStorage(currentName)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);

    producerBuilder.Write(Chimera::RS::Albedo).Format(VK_FORMAT_R8G8B8A8_UNORM);

    Chimera::SVGFPass::Config config;

    Chimera::RenderGraphPass combineGraphPass;
    combineGraphPass.name = "SVGFCombinePass";
    combineGraphPass.isCompute = true;

    Chimera::RenderGraph::PassBuilder combineBuilder(graph, combineGraphPass);

    Chimera::SVGFCombinePass combinePass(config, currentName);

    Chimera::SVGFCombineData data{};
    combinePass.Setup(data, combineBuilder);

    Require(combineGraphPass.inputs.size() == 2,
            "SVGF Combine should declare only current signal and albedo");

    Require(combineGraphPass.inputs[0].handle == data.current,
            "SVGF Combine binding 0 should contain current signal");

    Require(combineGraphPass.inputs[1].handle == data.albedo,
            "SVGF Combine binding 4 should contain albedo");
}

void TestMissingHistoryReadIsRejected()
{
    Chimera::RenderGraph graph(1280, 720);

    graph.AddPassRaw<EmptyPassData>(
        "MissingHistoryPass",
        [](EmptyPassData&, Chimera::RenderGraph::PassBuilder& builder)
        { builder.ReadHistory("MissingHistory"); },
        [](const EmptyPassData&, Chimera::RenderGraphRegistry&,
           VkCommandBuffer) {});

    bool rejected = false;

    try
    {
        graph.Compile();
    }
    catch (const std::logic_error& error)
    {
        const std::string message = error.what();

        Require(message.find("MissingHistoryPass") != std::string::npos,
                "missing-history diagnostic should contain the pass name");

        Require(message.find("MissingHistory") != std::string::npos,
                "missing-history diagnostic should contain the history name");

        rejected = true;
    }

    Require(rejected,
            "Compile accepted a required history resource that does not exist");
}

void TestGraphicsHistoryFallbackUsesGraphicsStage()
{
    Chimera::RenderGraph graph(1280, 720);

    Chimera::RenderGraphPass producerPass;
    Chimera::RenderGraph::PassBuilder producerBuilder(graph, producerPass);

    const auto fallback = static_cast<Chimera::RGResourceHandle>(
        producerBuilder.Write("FallbackColor")
            .Format(VK_FORMAT_R16G16B16A16_SFLOAT));

    Chimera::RenderGraphPass graphicsPass;
    graphicsPass.name = "GraphicsHistoryConsumer";
    graphicsPass.isCompute = false;

    Chimera::RenderGraph::PassBuilder builder(graph, graphicsPass);

    const auto result = builder.ReadHistorySafe(
        "MissingHistory", "FallbackColor", "historyColor");

    Require(result == fallback,
            "missing history should return the fallback resource");

    Require(graphicsPass.inputs.size() == 1,
            "history fallback should add exactly one input");

    Require(
        graphicsPass.inputs[0].usage == Chimera::ResourceUsage::GraphicsSampled,
        "graphics history fallback must use graphics sampled usage");

    Require(graphicsPass.inputs[0].bindingName == "historyColor",
            "graphics history fallback must preserve its shader binding name");
}

void TestDuplicateHistoryProducersAreRejected()
{
    Chimera::RenderGraph graph(1280, 720);

    graph.AddPassRaw<EmptyPassData>(
        "HistoryWriterA",
        [](EmptyPassData&, Chimera::RenderGraph::PassBuilder& builder)
        {
            builder.Write("HistorySourceA")
                .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                .SaveAsHistory("SharedHistory");
        },
        [](const EmptyPassData&, Chimera::RenderGraphRegistry&,
           VkCommandBuffer) {});

    graph.AddPassRaw<EmptyPassData>(
        "HistoryWriterB",
        [](EmptyPassData&, Chimera::RenderGraph::PassBuilder& builder)
        {
            builder.Write("HistorySourceB")
                .Format(VK_FORMAT_R16G16B16A16_SFLOAT)
                .SaveAsHistory("SharedHistory");
        },
        [](const EmptyPassData&, Chimera::RenderGraphRegistry&,
           VkCommandBuffer) {});

    bool rejected = false;

    try
    {
        graph.Compile();
    }
    catch (const std::logic_error& error)
    {
        const std::string message = error.what();

        Require(message.find("SharedHistory") != std::string::npos,
                "diagnostic should contain the duplicate history name");

        rejected = true;
    }

    Require(rejected,
            "Compile accepted multiple producers for one history resource");
}

void TestPhysicalImageUsageContract()
{
    using Chimera::ResourceUsage;
    using Chimera::SupportsImageUsage;

    Require(SupportsImageUsage(VK_IMAGE_USAGE_SAMPLED_BIT,
                               ResourceUsage::GraphicsSampled),
            "sampled image must support graphics sampled usage");

    Require(SupportsImageUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               ResourceUsage::TransferDst),
            "transfer-destination image must support transfer writes");

    Require(!SupportsImageUsage(VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                ResourceUsage::StorageWrite),
            "transfer-only image must reject storage writes");

    constexpr VkImageUsageFlags storageAndTransfer =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    Require(SupportsImageUsage(storageAndTransfer, ResourceUsage::StorageWrite),
            "combined image usage must support storage writes");

    Require(SupportsImageUsage(storageAndTransfer, ResourceUsage::TransferDst),
            "combined image usage must support transfer writes");

    Require(SupportsImageUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                               ResourceUsage::DepthStencilWrite),
            "depth-stencil image must support depth writes");

    Require(!SupportsImageUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                ResourceUsage::DepthStencilWrite),
            "color-attachment image must reject depth writes");

    Require(SupportsImageUsage(0, ResourceUsage::None),
            "resource usage None must not require an image usage flag");
}

void TestNamedDescriptorResolutionIgnoresRequestOrder()
{
    Chimera::RenderGraph graph(1280, 720);
    Chimera::RenderGraphPass pass;
    pass.name = "NamedDescriptorPass";

    Chimera::ResourceRequest depth{30, Chimera::ResourceUsage::ComputeSampled};
    depth.bindingName = "gDepth";

    Chimera::ResourceRequest current{
        10, Chimera::ResourceUsage::ComputeSampled};
    current.bindingName = "curColor";

    Chimera::ResourceRequest motion{
        20, Chimera::ResourceUsage::ComputeSampled};
    motion.bindingName = "gMotion";

    // Deliberately differs from shader binding order.
    pass.inputs = {depth, current, motion};

    Chimera::ResourceRequest output{40, Chimera::ResourceUsage::StorageWrite};
    output.bindingName = "outFinal";
    pass.outputs = {output};

    TestExecutionContext context(graph, pass, VK_NULL_HANDLE);

    Require(context.UsesNamedBindings(),
            "a pass with named requests must enable named resolution");

    const Chimera::ShaderResource motionBinding{
        "gMotion", 2, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    const Chimera::ShaderResource depthBinding{
        "gDepth", 2, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    const Chimera::ShaderResource currentBinding{
        "curColor", 2, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    const Chimera::ShaderResource outputBinding{
        "outFinal", 2, 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};

    Require(context.ResolveNamedImageBinding(motionBinding) == motion.handle,
            "gMotion must resolve by name instead of input position");
    Require(context.ResolveNamedImageBinding(depthBinding) == depth.handle,
            "gDepth must resolve by name instead of input position");
    Require(context.ResolveNamedImageBinding(currentBinding) == current.handle,
            "curColor must resolve by name instead of input position");
    Require(context.ResolveNamedImageBinding(outputBinding) == output.handle,
            "storage binding must resolve from pass outputs");

    Chimera::RenderGraphPass unnamedPass;
    unnamedPass.name = "LegacyDescriptorPass";
    unnamedPass.inputs.push_back(
        {50, Chimera::ResourceUsage::ComputeSampled});

    TestExecutionContext unnamedContext(graph, unnamedPass, VK_NULL_HANDLE);
    Require(!unnamedContext.UsesNamedBindings(),
            "a pass without binding names must retain positional mode");
}

void TestNamedDescriptorResolutionRejectsInvalidContracts()
{
    Chimera::RenderGraph graph(1280, 720);
    Chimera::RenderGraphPass pass;
    pass.name = "BrokenNamedDescriptorPass";

    Chimera::ResourceRequest motionA{
        10, Chimera::ResourceUsage::ComputeSampled};
    motionA.bindingName = "gMotion";
    Chimera::ResourceRequest motionB{
        20, Chimera::ResourceUsage::ComputeSampled};
    motionB.bindingName = "gMotion";
    pass.inputs = {motionA, motionB};

    TestExecutionContext context(graph, pass, VK_NULL_HANDLE);

    const Chimera::ShaderResource missingBinding{
        "missingBinding", 2, 7,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};

    bool missingRejected = false;
    try
    {
        context.ResolveNamedImageBinding(missingBinding);
    }
    catch (const std::logic_error& error)
    {
        const std::string message = error.what();
        missingRejected =
            message.find(pass.name) != std::string::npos &&
            message.find(missingBinding.name) != std::string::npos;
    }

    Require(missingRejected,
            "missing named descriptor must produce a useful diagnostic");

    const Chimera::ShaderResource duplicateBinding{
        "gMotion", 2, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};

    bool duplicateRejected = false;
    try
    {
        context.ResolveNamedImageBinding(duplicateBinding);
    }
    catch (const std::logic_error& error)
    {
        const std::string message = error.what();
        duplicateRejected =
            message.find(pass.name) != std::string::npos &&
            message.find(duplicateBinding.name) != std::string::npos;
    }

    Require(duplicateRejected,
            "duplicate named descriptors must be rejected");
}

} // namespace

int main()
{
    Chimera::Log::Init();
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

        TestTAAFirstFramePreservesHistoryBinding();
        std::cout << "[PASS] TAA first frame preserves history binding\n";

        TestSVGFCombineUsesOnlyShaderInputs();
        std::cout << "[PASS] SVGF Combine declares only used shader inputs\n";

        TestMissingHistoryReadIsRejected();
        std::cout << "[PASS] missing required history is rejected\n";

        TestGraphicsHistoryFallbackUsesGraphicsStage();
        std::cout << "[PASS] graphics history fallback uses graphics stage\n";

        TestDuplicateHistoryProducersAreRejected();
        std::cout << "[PASS] duplicate history producers are rejected\n";

        TestPhysicalImageUsageContract();
        std::cout << "[PASS] physical image usage contract is enforced\n";

        TestNamedDescriptorResolutionIgnoresRequestOrder();
        std::cout << "[PASS] named descriptor resolution ignores request order\n";

        TestNamedDescriptorResolutionRejectsInvalidContracts();
        std::cout << "[PASS] invalid named descriptor contracts are rejected\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}
