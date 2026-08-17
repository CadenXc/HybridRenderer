#include "Core/Log.h"
#include "Renderer/Graph/RenderGraph.h"
#include "Renderer/Graph/ResourceNames.h"
#include "Renderer/Graph/ExecutionContext.h"
#include "Renderer/Backend/Shader.h"
#include "Renderer/Passes/CompositionPass.h"
#include "Renderer/Passes/RTShadowPass.h"
#include "Renderer/Passes/TAAPass.h"
#include "Renderer/Passes/SVGFPass.h"

#include <array>
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

    Require(graph.GetTimingSampleId() == 0,
        "empty graph must not publish a GPU timing sample");
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

void TestMixedDescriptorBindingsAreRejected()
{
    Chimera::RenderGraph graph(1280, 720);

    Chimera::RenderGraphPass producerPass;
    Chimera::RenderGraph::PassBuilder producerBuilder(graph, producerPass);
    producerBuilder.Write("MixedInputA").Format(VK_FORMAT_R8G8B8A8_UNORM);
    producerBuilder.Write("MixedInputB").Format(VK_FORMAT_R8G8B8A8_UNORM);

    graph.AddPassRaw<EmptyPassData>(
        "MixedDescriptorPass",
        [](EmptyPassData&, Chimera::RenderGraph::PassBuilder& builder)
        {
            builder.ReadCompute("MixedInputA", "namedInput");
            builder.ReadCompute("MixedInputB");
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

        Require(message.find("MixedDescriptorPass") != std::string::npos,
                "mixed-descriptor diagnostic should contain the pass name");
        Require(message.find("mixes named and unnamed descriptor resources") !=
                    std::string::npos,
                "mixed-descriptor diagnostic should explain the contract");

        rejected = true;
    }

    Require(rejected,
            "Compile accepted mixed named and unnamed descriptor resources");
}

void TestUnnamedAttachmentIsAllowedWithNamedDescriptors()
{
    Chimera::RenderGraph graph(1280, 720);

    Chimera::RenderGraphPass producerPass;
    Chimera::RenderGraph::PassBuilder producerBuilder(graph, producerPass);
    producerBuilder.Write("InputColor").Format(VK_FORMAT_R8G8B8A8_UNORM);

    graph.AddPassRaw<EmptyPassData>(
        "NamedInputColorOutputPass",
        [](EmptyPassData&, Chimera::RenderGraph::PassBuilder& builder)
        {
            builder.Read("InputColor", "inColor");
            builder.Write("OutputColor")
                .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
        },
        [](const EmptyPassData&, Chimera::RenderGraphRegistry&,
           VkCommandBuffer) {});

    graph.Compile();
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

    Require(taaGraphPass.inputs.size() == 5,
            "TAA first frame must preserve all five input bindings");

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

    Require(taaGraphPass.inputs[4].handle == data.historyDepth,
            "TAA binding 4 should contain history depth fallback");

    Require(data.historyDepth == data.depth,
            "TAA first frame should use current depth as history fallback");

    Require(taaGraphPass.inputs[0].bindingName == "curColor",
            "TAA current color must bind to curColor");

    Require(taaGraphPass.inputs[1].bindingName == "historyColor",
            "TAA history fallback must preserve historyColor binding");

    Require(taaGraphPass.inputs[2].bindingName == "gMotion",
            "TAA motion must bind to gMotion");

    Require(taaGraphPass.inputs[3].bindingName == "gDepth",
            "TAA depth must bind to gDepth");

    Require(taaGraphPass.inputs[4].bindingName == "historyDepth",
            "TAA history depth fallback must preserve its named binding");

    Require(taaGraphPass.outputs.size() == 1,
            "TAA must declare exactly one output");

    Require(taaGraphPass.outputs[0].bindingName == "outFinal",
            "TAA output must bind to outFinal");
}

void TestRTShadowUsesRaytraceNamedBindings()
{
    Chimera::RenderGraph graph(1280, 720);

    Chimera::RenderGraphPass producerPass;
    Chimera::RenderGraph::PassBuilder producerBuilder(graph, producerPass);

    producerBuilder.Write(Chimera::RS::Normal)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(Chimera::RS::Depth).Format(VK_FORMAT_D32_SFLOAT);

    Chimera::RenderGraphPass shadowGraphPass;
    shadowGraphPass.name = "RTShadowPass";

    Chimera::RenderGraph::PassBuilder shadowBuilder(graph, shadowGraphPass);
    Chimera::RTShadowPass shadowPass(std::shared_ptr<Chimera::Scene>{});
    Chimera::RTShadowPassData data{};

    shadowPass.Setup(data, shadowBuilder);

    Require(shadowGraphPass.inputs.size() == 2,
            "RT shadow must declare normal and depth inputs");
    Require(shadowGraphPass.outputs.size() == 1,
            "RT shadow must declare exactly one output");

    Require(shadowGraphPass.inputs[0].handle == data.normal,
            "RT shadow normal input must preserve its handle");
    Require(shadowGraphPass.inputs[0].usage ==
                Chimera::ResourceUsage::RaytraceSampled,
            "RT shadow normal must use the ray tracing shader stage");
    Require(shadowGraphPass.inputs[0].bindingName == "gNormal",
            "RT shadow normal must bind to gNormal");

    Require(shadowGraphPass.inputs[1].handle == data.depth,
            "RT shadow depth input must preserve its handle");
    Require(shadowGraphPass.inputs[1].usage ==
                Chimera::ResourceUsage::RaytraceSampled,
            "RT shadow depth must use the ray tracing shader stage");
    Require(shadowGraphPass.inputs[1].bindingName == "gDepth",
            "RT shadow depth must bind to gDepth");

    Require(shadowGraphPass.outputs[0].handle == data.output,
            "RT shadow output must preserve its handle");
    Require(shadowGraphPass.outputs[0].usage ==
                Chimera::ResourceUsage::StorageWrite,
            "RT shadow output must be a storage write");
    Require(shadowGraphPass.outputs[0].bindingName == "rtShadowAO",
            "RT shadow output must bind to rtShadowAO");
}

void TestCompositionPreservesPackedShadowAOBindings()
{
    Chimera::RenderGraph graph(1280, 720);

    const std::string giName = "TestGI";
    const std::string reflectionName = "TestReflection";
    const std::string packedShadowAOName = "PackedShadowAO";

    Chimera::RenderGraphPass producerPass;
    Chimera::RenderGraph::PassBuilder producerBuilder(graph, producerPass);

    producerBuilder.Write(Chimera::RS::Albedo)
        .Format(VK_FORMAT_R8G8B8A8_UNORM);
    producerBuilder.Write(Chimera::RS::Normal)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(Chimera::RS::MaterialParams)
        .Format(VK_FORMAT_R8G8B8A8_UNORM);
    producerBuilder.Write(Chimera::RS::Motion)
        .Format(VK_FORMAT_R16G16_SFLOAT);
    producerBuilder.Write(Chimera::RS::Depth).Format(VK_FORMAT_D32_SFLOAT);
    producerBuilder.Write(Chimera::RS::Emissive)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(giName).Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(reflectionName)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(packedShadowAOName)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);

    Chimera::CompositionPass::Config config;
    config.giName = giName;
    config.reflectionName = reflectionName;
    config.shadowName = packedShadowAOName;
    config.aoName = packedShadowAOName;

    Chimera::RenderGraphPass compositionGraphPass;
    compositionGraphPass.name = "Composition";

    Chimera::RenderGraph::PassBuilder compositionBuilder(
        graph, compositionGraphPass);
    Chimera::CompositionPass compositionPass(config);
    Chimera::CompositionPassData data{};

    compositionPass.Setup(data, compositionBuilder);

    Require(data.shadow_raw == data.ao_raw,
            "Composition packed shadow and AO must share one image handle");

    const std::array<const char*, 10> expectedBindings = {
        "gAlbedo",   "gNormal",     "gMaterialParams", "gMotion",
        "gDepth",    "gEmissive",   "gGI",             "gReflection",
        "gShadow",   "gAO"};
    const std::array<Chimera::RGResourceHandle, 10> expectedHandles = {
        data.albedo,         data.normal,         data.material,
        data.motion,         data.depth,          data.emissive,
        data.gi_raw,         data.reflection_raw, data.shadow_raw,
        data.ao_raw};

    Require(compositionGraphPass.inputs.size() == expectedBindings.size(),
            "Composition must declare all ten sampled inputs");

    for (size_t i = 0; i < expectedBindings.size(); ++i)
    {
        Require(compositionGraphPass.inputs[i].handle == expectedHandles[i],
                "Composition input must preserve its resource handle");
        Require(compositionGraphPass.inputs[i].usage ==
                    Chimera::ResourceUsage::GraphicsSampled,
                "Composition input must use the graphics shader stage");
        Require(compositionGraphPass.inputs[i].bindingName ==
                    expectedBindings[i],
                "Composition input must preserve its reflected shader name");
    }

    Require(compositionGraphPass.inputs[8].handle ==
                compositionGraphPass.inputs[9].handle,
            "Composition shadow and AO requests must share the packed image");
    Require(compositionGraphPass.inputs[8].bindingName == "gShadow",
            "Composition packed shadow must bind to gShadow");
    Require(compositionGraphPass.inputs[9].bindingName == "gAO",
            "Composition packed AO must bind to gAO");

    Require(compositionGraphPass.outputs.size() == 1,
            "Composition must declare exactly one color output");
    Require(compositionGraphPass.outputs[0].handle == data.output,
            "Composition output must preserve its resource handle");
    Require(compositionGraphPass.outputs[0].usage ==
                Chimera::ResourceUsage::ColorAttachment,
            "Composition output must be a color attachment");
    Require(compositionGraphPass.outputs[0].bindingName.empty(),
            "Composition color attachment must not declare a descriptor name");
}

void TestSVGFTemporalFirstFramePreservesNamedBindings()
{
    Chimera::RenderGraph graph(1280, 720);
    Chimera::SVGFPass::Config config;

    Chimera::RenderGraphPass producerPass;
    Chimera::RenderGraph::PassBuilder producerBuilder(graph, producerPass);

    producerBuilder.WriteStorage(config.inputName)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(Chimera::RS::Motion)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(Chimera::RS::Depth).Format(VK_FORMAT_D32_SFLOAT);
    producerBuilder.Write(Chimera::RS::Normal)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(Chimera::RS::ObjectID).Format(VK_FORMAT_R32_UINT);
    Chimera::RGResourceHandle albedo =
        producerBuilder.Write(Chimera::RS::Albedo)
            .Format(VK_FORMAT_R8G8B8A8_UNORM);

    Chimera::RenderGraphPass temporalGraphPass;
    temporalGraphPass.name = "SVGFTemporalPass";
    temporalGraphPass.isCompute = true;

    Chimera::RenderGraph::PassBuilder temporalBuilder(graph,
                                                       temporalGraphPass);
    Chimera::SVGFTemporalPass temporalPass(config);
    Chimera::SVGFTemporalData data{};

    temporalPass.Setup(data, temporalBuilder);

    Require(data.history == data.cur,
            "SVGF temporal signal history should fall back to current signal");
    Require(data.historyMoments == data.cur,
            "SVGF temporal moments history should fall back to current signal");
    Require(data.prevDepth == data.depth,
            "SVGF previous depth should fall back to current depth");
    Require(data.prevNormal == data.normal,
            "SVGF previous normal should fall back to current normal");
    Require(data.prevObjectID == data.objectID,
            "SVGF previous object ID should fall back to current object ID");
    Require(data.prevMotion == data.motion,
            "SVGF previous motion should fall back to current motion");

    const std::array<const char*, 12> expectedBindings = {
        "gCurSignal",   "gMotion",       "gHistorySignal",
        "gHistoryMoments", "gCurDepth", "gCurNormal",
        "gPrevDepth",  "gPrevNormal",   "gCurObjectID",
        "gPrevObjectID", "gPrevMotion", "gAlbedo"};
    const std::array<Chimera::RGResourceHandle, 12> expectedHandles = {
        data.cur,          data.motion,       data.history,
        data.historyMoments, data.depth,      data.normal,
        data.prevDepth,    data.prevNormal,   data.objectID,
        data.prevObjectID, data.prevMotion,   albedo};

    Require(temporalGraphPass.inputs.size() == expectedBindings.size(),
            "SVGF temporal must declare all twelve sampled inputs");

    for (size_t i = 0; i < expectedBindings.size(); ++i)
    {
        Require(temporalGraphPass.inputs[i].handle == expectedHandles[i],
                "SVGF temporal input must preserve its resource handle");
        Require(temporalGraphPass.inputs[i].bindingName ==
                    expectedBindings[i],
                "SVGF temporal input must preserve its reflected shader name");
    }

    Require(temporalGraphPass.outputs.size() == 2,
            "SVGF temporal must declare two storage outputs");
    Require(temporalGraphPass.outputs[0].handle == data.output,
            "SVGF temporal signal output must preserve its handle");
    Require(temporalGraphPass.outputs[0].bindingName == "outSignal",
            "SVGF temporal signal output must bind to outSignal");
    Require(temporalGraphPass.outputs[1].handle == data.outMoments,
            "SVGF temporal moments output must preserve its handle");
    Require(temporalGraphPass.outputs[1].bindingName == "outMoments",
            "SVGF temporal moments output must bind to outMoments");
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
    Require(combineGraphPass.inputs[0].bindingName == "gCurrentFiltered",
            "SVGF Combine current signal must bind to gCurrentFiltered");

    Require(combineGraphPass.inputs[1].bindingName == "gAlbedo",
            "SVGF Combine albedo must bind to gAlbedo");

    Require(combineGraphPass.outputs.size() == 1,
            "SVGF Combine must declare one output");

    Require(combineGraphPass.outputs[0].bindingName == "outFinal",
            "SVGF Combine output must bind to outFinal");
}

void TestSVGFAtrousUsesNamedShaderBindings()
{
    Chimera::RenderGraph graph(1280, 720);

    const std::string inputName = "TestAtrousInput";
    const std::string outputName = "TestAtrousOutput";

    Chimera::RenderGraphPass producerPass;
    Chimera::RenderGraph::PassBuilder producerBuilder(graph, producerPass);

    producerBuilder.WriteStorage(inputName)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(Chimera::RS::Normal)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(Chimera::RS::Motion)
        .Format(VK_FORMAT_R16G16B16A16_SFLOAT);
    producerBuilder.Write(Chimera::RS::ObjectID)
        .Format(VK_FORMAT_R32_UINT);
    producerBuilder.Write(Chimera::RS::MaterialParams)
        .Format(VK_FORMAT_R8G8B8A8_UNORM);

    Chimera::RenderGraphPass atrousGraphPass;
    atrousGraphPass.name = "SVGFAtrousPass";
    atrousGraphPass.isCompute = true;

    Chimera::RenderGraph::PassBuilder atrousBuilder(graph, atrousGraphPass);
    Chimera::SVGFPass::Config config;
    Chimera::SVGFAtrousPass atrousPass(config, 0, inputName, outputName, {});
    Chimera::SVGFAtrousData data{};

    atrousPass.Setup(data, atrousBuilder);

    Require(atrousGraphPass.inputs.size() == 5,
            "SVGF A-trous must declare all five sampled inputs");

    const std::array<const char*, 5> expectedInputBindings = {
        "gInputColor", "gNormal", "gMotion", "gObjectID",
        "gMaterialParams"};
    const std::array<Chimera::RGResourceHandle, 5> expectedInputHandles = {
        data.input, data.normal, data.motion, data.objectID,
        data.materialParams};

    for (size_t i = 0; i < expectedInputBindings.size(); ++i)
    {
        Require(atrousGraphPass.inputs[i].handle == expectedInputHandles[i],
                "SVGF A-trous input must preserve its resource handle");
        Require(atrousGraphPass.inputs[i].bindingName ==
                    expectedInputBindings[i],
                "SVGF A-trous input must use its reflected shader name");
    }

    Require(atrousGraphPass.outputs.size() == 1,
            "SVGF A-trous must declare exactly one output");
    Require(atrousGraphPass.outputs[0].handle == data.output,
            "SVGF A-trous output must preserve its resource handle");
    Require(atrousGraphPass.outputs[0].bindingName == "outFiltered",
            "SVGF A-trous output must bind to outFiltered");
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

        TestMixedDescriptorBindingsAreRejected();
        std::cout << "[PASS] mixed descriptor contracts are rejected\n";

        TestUnnamedAttachmentIsAllowedWithNamedDescriptors();
        std::cout
            << "[PASS] attachments are excluded from descriptor contracts\n";

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

        TestRTShadowUsesRaytraceNamedBindings();
        std::cout << "[PASS] RT shadow uses ray tracing named bindings\n";

        TestCompositionPreservesPackedShadowAOBindings();
        std::cout << "[PASS] Composition preserves packed shadow AO bindings\n";

        TestSVGFTemporalFirstFramePreservesNamedBindings();
        std::cout
            << "[PASS] SVGF temporal first frame preserves named bindings\n";

        TestSVGFCombineUsesOnlyShaderInputs();
        std::cout << "[PASS] SVGF Combine declares only used shader inputs\n";

        TestSVGFAtrousUsesNamedShaderBindings();
        std::cout << "[PASS] SVGF A-trous uses named shader bindings\n";

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
