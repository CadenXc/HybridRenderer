#include "pch.h"
#include "ImGuiLayer.h"
#include "Renderer/ChimeraCommon.h"
#include "Renderer/Resources/ResourceManager.h"
#include "Utils/VulkanBarrier.h"
#include "Core/Application.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>

namespace Chimera
{
    ImGuiLayer::ImGuiLayer(const std::shared_ptr<VulkanContext>& context)
        : Layer("ImGuiLayer"), m_Context(context)
    {
    }

    ImGuiLayer::~ImGuiLayer()
    {
    }

    void ImGuiLayer::OnAttach()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
        
        ImGui::StyleColorsDark();
        SetDarkThemeColors();

        ImGui_ImplGlfw_InitForVulkan(m_Context->GetWindow(), false);
        
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_Context->GetInstance();
        init_info.PhysicalDevice = m_Context->GetPhysicalDevice();
        init_info.Device = m_Context->GetDevice();
        init_info.QueueFamily = m_Context->GetGraphicsQueueFamily();
        init_info.Queue = m_Context->GetGraphicsQueue();
        init_info.DescriptorPool = ResourceManager::Get().GetDescriptorPool();
        init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
        init_info.ImageCount = MAX_FRAMES_IN_FLIGHT;
        init_info.UseDynamicRendering = true;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        
        static VkFormat swapchainFormat; 
        swapchainFormat = m_Context->GetSwapChainImageFormat();
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;

        ImGui_ImplVulkan_Init(&init_info);
    }

    void ImGuiLayer::OnDetach()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        ClearTextureCache();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiLayer::OnEvent(Event& e)
    {
        ImGuiIO& io = ImGui::GetIO();
        
        if (e.GetEventType() == EventType::MouseScrolled)
        {
            return;
        }

        e.Handled |= e.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
        e.Handled |= e.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
    }

    void ImGuiLayer::Begin()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);
    }

    void ImGuiLayer::End(VkCommandBuffer commandBuffer)
    {
        ImGui::Render();

        VkExtent2D extent = m_Context->GetSwapChainExtent();
        uint32_t imageIndex = Application::Get().GetCurrentImageIndex();
        
        VkImage targetImage = m_Context->GetSwapChainImages()[imageIndex];
        VkImageView targetView = m_Context->GetSwapchain()->GetImageViews()[imageIndex];
        VkFormat format = m_Context->GetSwapChainImageFormat();

        VkRenderingAttachmentInfoKHR colorAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
        colorAttachment.imageView = targetView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; 
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        
        VkRenderingInfoKHR renderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
        renderingInfo.renderArea = { {0, 0}, extent };
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        
        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        vkCmdEndRendering(commandBuffer);

        VulkanUtils::TransitionImageLayout(commandBuffer, targetImage, format, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    ImTextureID ImGuiLayer::GetTextureID(VkImageView view, VkSampler sampler)
    {
        if (view == VK_NULL_HANDLE)
        {
            return (ImTextureID)0;
        }
        if (sampler == VK_NULL_HANDLE)
        {
            sampler = ResourceManager::Get().GetDefaultSampler();
        }
        
        if (m_TextureCache.count(view))
        {
            return m_TextureCache[view];
        }
        
        ImTextureID id = (ImTextureID)ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_TextureCache[view] = id;
        return id;
    }

    void ImGuiLayer::ClearTextureCache()
    {
        for (auto& [view, id] : m_TextureCache)
        {
            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)id);
        }
        m_TextureCache.clear(); 
    }

    void ImGuiLayer::SetDarkThemeColors()
    {
        auto& colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };
        colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
        colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
        colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    }
}
