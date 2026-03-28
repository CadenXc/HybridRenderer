#pragma once

#include "RenderGraphCommon.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <deque>
#include <type_traits>

namespace Chimera
{
    class VulkanContext;

    struct PhysicalResource
    {
        std::string name; 
        std::string historyName;
        ImageDescription desc;
        GraphImage image;
        ResourceState currentState;
        uint32_t firstPass = 0xFFFFFFFF;
        uint32_t lastPass = 0;
    };

    struct HistoryResource
    {
        GraphImage image;
        ResourceState state;
    };

    class RenderGraph
    {
    public:
        struct PassBuilder
        {
            RenderGraph& graph;
            struct RenderGraphPass& pass;
            PassBuilder(RenderGraph& g, struct RenderGraphPass& p) : graph(g), pass(p) {}

            RGResourceHandle Read(const std::string& name);
            RGResourceHandle ReadCompute(const std::string& name);
            RGResourceHandle ReadHistory(const std::string& name);
            
            ResourceHandleProxy Write(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED);
            ResourceHandleProxy WriteStorage(const std::string& name, VkFormat format = VK_FORMAT_UNDEFINED);
        };

        RenderGraph(VulkanContext& context, uint32_t w, uint32_t h);
        ~RenderGraph();

        /**
         * @brief Raw Lambda-based AddPass.
         */
        template<typename PassData>
        void AddPassRaw(const std::string& name,
                       std::function<void(PassData&, PassBuilder&)> setup,
                       std::function<void(const PassData&, RenderGraphRegistry&, VkCommandBuffer)> execute)
        {
            auto& pass = m_PassStack.emplace_back();
            pass.name = name;
            pass.width = m_Width;
            pass.height = m_Height;
            auto data = std::make_shared<PassData>();
            PassBuilder builder(*this, pass);
            setup(*data, builder);
            pass.executeFunc = [=](RenderGraphRegistry& reg, VkCommandBuffer cmd) { execute(*data, reg, cmd); };
        }

        /**
         * @brief Class-based AddPass. Supports both atomic RenderPass subclasses 
         * and Compound Pass types (which define their own static Add() method).
         */
        template<typename T, typename... Args>
        void AddPass(Args&&... args)
        {
            if constexpr (HasPassData<T>::value)
            {
                auto passInstance = std::make_shared<T>(std::forward<Args>(args)...);
                using Data = typename T::PassData;

                if constexpr (HasExecuteGraphics<T, Data>::value)
                {
                    this->AddGraphicsPass<Data>(
                        T::Name,
                        [passInstance](Data& data, PassBuilder& builder) {
                            passInstance->Setup(data, builder);
                        },
                        [passInstance](const Data& data, class GraphicsExecutionContext& ctx) {
                            passInstance->Execute(data, ctx);
                        }
                    );
                }
                else if constexpr (HasExecuteCompute<T, Data>::value)
                {
                    this->AddComputePass<Data>(
                        T::Name,
                        [passInstance](Data& data, PassBuilder& builder) {
                            passInstance->Setup(data, builder);
                        },
                        [passInstance](const Data& data, class ComputeExecutionContext& ctx) {
                            passInstance->Execute(data, ctx);
                        }
                    );
                }
                else if constexpr (HasExecuteRaytracing<T, Data>::value)
                {
                    this->AddRaytracingPass<Data>(
                        T::Name,
                        [passInstance](Data& data, PassBuilder& builder) {
                            passInstance->Setup(data, builder);
                        },
                        [passInstance](const Data& data, class RaytracingExecutionContext& ctx) {
                            passInstance->Execute(data, ctx);
                        }
                    );
                }
                else
                {
                    // Fallback to generic RenderPass
                    this->AddPassRaw<Data>(
                        T::Name,
                        [passInstance](Data& data, PassBuilder& builder) {
                            passInstance->Setup(data, builder);
                        },
                        [passInstance](const Data& data, RenderGraphRegistry& reg, VkCommandBuffer cmd) {
                            passInstance->Execute(data, reg, cmd);
                        }
                    );
                }
            }
            else
            {
                T::Add(*this, std::forward<Args>(args)...);
            }
        }

        template<typename PassData>
        void AddGraphicsPass(const std::string& name,
                           std::function<void(PassData&, PassBuilder&)> setup,
                           std::function<void(const PassData&, class GraphicsExecutionContext&)> execute)
        {
            auto& pass = m_PassStack.emplace_back();
            pass.name = name;
            pass.width = m_Width;
            pass.height = m_Height;
            auto data = std::make_shared<PassData>();
            PassBuilder builder(*this, pass);
            setup(*data, builder);
            
            pass.executeFunc = [=](RenderGraphRegistry& reg, VkCommandBuffer cmd) {
                GraphicsExecutionContext ctx(reg.graph, reg.pass, cmd);
                execute(*data, ctx);
            };
        }

        template<typename PassData>
        void AddComputePass(const std::string& name,
                           std::function<void(PassData&, PassBuilder&)> setup,
                           std::function<void(const PassData&, class ComputeExecutionContext&)> execute)
        {
            auto& pass = m_PassStack.emplace_back();
            pass.name = name;
            pass.isCompute = true;
            pass.width = m_Width;
            pass.height = m_Height;
            auto data = std::make_shared<PassData>();
            PassBuilder builder(*this, pass);
            setup(*data, builder);
            
            pass.executeFunc = [=](RenderGraphRegistry& reg, VkCommandBuffer cmd) {
                ComputeExecutionContext ctx(reg.graph, reg.pass, cmd);
                execute(*data, ctx);
            };
        }

        template<typename PassData>
        void AddRaytracingPass(const std::string& name,
                           std::function<void(PassData&, PassBuilder&)> setup,
                           std::function<void(const PassData&, class RaytracingExecutionContext&)> execute)
        {
            auto& pass = m_PassStack.emplace_back();
            pass.name = name;
            pass.width = m_Width;
            pass.height = m_Height;
            auto data = std::make_shared<PassData>();
            PassBuilder builder(*this, pass);
            setup(*data, builder);
            
            pass.executeFunc = [=](RenderGraphRegistry& reg, VkCommandBuffer cmd) {
                RaytracingExecutionContext ctx(reg.graph, reg.pass, cmd);
                execute(*data, ctx);
            };
        }

        void Reset();
        void Compile();
        VkSemaphore Execute(VkCommandBuffer cmd);
        void DestroyResources(bool all = false);

        void SetExternalResource(const std::string& name, VkImage image, VkImageView view, VkImageLayout layout, const ImageDescription& desc);

        RGResourceHandle GetResourceHandle(const std::string& name);
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        
        bool ContainsImage(const std::string& name);
        bool HasHistory(const std::string& name) const;
        const GraphImage& GetImage(const std::string& name) const;
        
        std::vector<std::string> GetDebuggableResources() const;
        const std::vector<PassTiming>& GetLatestTimings() const { return m_LatestTimings; }
        
        void DrawPerformanceStatistics();
        std::string ExportToMermaid() const;

    private:
        template<typename T, typename = void>
        struct HasPassData : std::false_type {};
        template<typename T>
        struct HasPassData<T, std::void_t<typename T::PassData>> : std::true_type {};

        template<typename T, typename Data, typename = void>
        struct HasExecuteGraphics : std::false_type {};
        template<typename T, typename Data>
        struct HasExecuteGraphics<T, Data, std::void_t<decltype(std::declval<T>().Execute(std::declval<const Data&>(), std::declval<class GraphicsExecutionContext&>()))>> : std::true_type {};

        template<typename T, typename Data, typename = void>
        struct HasExecuteCompute : std::false_type {};
        template<typename T, typename Data>
        struct HasExecuteCompute<T, Data, std::void_t<decltype(std::declval<T>().Execute(std::declval<const Data&>(), std::declval<class ComputeExecutionContext&>()))>> : std::true_type {};

        template<typename T, typename Data, typename = void>
        struct HasExecuteRaytracing : std::false_type {};
        template<typename T, typename Data>
        struct HasExecuteRaytracing<T, Data, std::void_t<decltype(std::declval<T>().Execute(std::declval<const Data&>(), std::declval<class RaytracingExecutionContext&>()))>> : std::true_type {};

        void BuildBarriers(VkCommandBuffer cmd, struct RenderGraphPass& pass, uint32_t passIdx);
        void BeginPassDebugLabel(VkCommandBuffer cmd, const struct RenderGraphPass& pass);
        void EndPassDebugLabel(VkCommandBuffer cmd);
        void WriteTimestamp(VkCommandBuffer cmd, uint32_t queryIdx, VkPipelineStageFlags2 stage);
        bool BeginDynamicRendering(VkCommandBuffer cmd, const struct RenderGraphPass& pass);
        void UpdatePersistentResources(VkCommandBuffer cmd);

    private:
        VulkanContext& m_Context;
        uint32_t m_Width, m_Height;
        std::vector<struct RenderGraphPass> m_PassStack;
        std::vector<PhysicalResource> m_Resources;
        std::unordered_map<std::string, RGResourceHandle> m_ResourceMap;
        std::unordered_map<std::string, HistoryResource> m_HistoryResources;
        std::unordered_map<VkImage, ResourceState> m_ExternalImageStates;
        std::unordered_map<VkImage, ResourceState> m_PhysicalImageStates;

        VkCommandPool m_ComputeCommandPool = VK_NULL_HANDLE;
        VkCommandBuffer m_ComputeCommandBuffer = VK_NULL_HANDLE;
        VkSemaphore m_ComputeFinishedSemaphore = VK_NULL_HANDLE;
        VkSemaphore m_GraphicsWaitSemaphore = VK_NULL_HANDLE;

        VkQueryPool m_TimestampQueryPool = VK_NULL_HANDLE;
        std::vector<PassTiming> m_LatestTimings;
        std::vector<std::string> m_LastPassNames;
        uint32_t m_PreviousPassCount = 0;

        std::vector<PooledImage> m_ImagePool;

        friend class GraphicsExecutionContext;
        friend class ComputeExecutionContext;
        friend class RaytracingExecutionContext;
        friend struct RenderGraphRegistry;
        friend class ResourceHandleProxy;
    };
}
