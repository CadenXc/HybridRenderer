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
    OnDetach();
}

void ImGuiLayer::OnAttach()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard |
                      ImGuiConfigFlags_DockingEnable |
                      ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();
    SetDarkThemeColors();

    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    vkCreateDescriptorPool(m_Context->GetDevice(), &pool_info, nullptr,
                           &m_Pool);

    ImGui_ImplGlfw_InitForVulkan(m_Context->GetWindow(), false);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_Context->GetInstance();
    init_info.PhysicalDevice = m_Context->GetPhysicalDevice();
    init_info.Device = m_Context->GetDevice();
    init_info.QueueFamily = m_Context->GetGraphicsQueueFamily();
    init_info.Queue = m_Context->GetGraphicsQueue();
    init_info.DescriptorPool = m_Pool;
    init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    init_info.ImageCount = MAX_FRAMES_IN_FLIGHT;
    init_info.UseDynamicRendering = true;

    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo
        .colorAttachmentCount = 1;

    static VkFormat swapchainFormat;
    swapchainFormat = m_Context->GetSwapChainImageFormat();
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo
        .pColorAttachmentFormats = &swapchainFormat;

    ImGui_ImplVulkan_Init(&init_info);
}

void ImGuiLayer::OnDetach()
{
    if (!ImGui::GetCurrentContext())
    {
        return;
    }

    if (m_Context && m_Context->GetDevice() != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_Context->GetDevice());
        ClearTextureCache();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if (m_Pool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(m_Context->GetDevice(), m_Pool, nullptr);
            m_Pool = VK_NULL_HANDLE;
        }
    }
}

void ImGuiLayer::OnEvent(Event& e)
{
    if (ImGui::GetCurrentContext())
    {
        ImGuiIO& io = ImGui::GetIO();
        e.Handled |= e.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
        e.Handled |=
            e.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
    }
}

void ImGuiLayer::Begin()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }
}

void ImGuiLayer::End(VkCommandBuffer commandBuffer)
{
    if (ImGui::GetCurrentContext())
    {
        ImGui::Render();

        VkExtent2D extent = m_Context->GetSwapChainExtent();
        uint32_t imageIndex = Application::Get().GetCurrentImageIndex();
        VkImageView targetView =
            m_Context->GetSwapchain()->GetImageViews()[imageIndex];

        VkRenderingAttachmentInfo colorAttachment = {
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colorAttachment.imageView = targetView;
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo = {VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderingInfo.renderArea = {{0, 0}, extent};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        vkCmdEndRendering(commandBuffer);

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }
}

ImTextureID ImGuiLayer::GetTextureID(VkImageView view, VkSampler sampler)
{
    if (view == VK_NULL_HANDLE || !ImGui::GetCurrentContext())
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

    ImTextureID id = (ImTextureID)ImGui_ImplVulkan_AddTexture(
        sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_TextureCache[view] = id;
    return id;
}

void ImGuiLayer::ClearTextureCache()
{
    if (!ImGui::GetCurrentContext())
    {
        return;
    }

    for (auto& [view, id] : m_TextureCache)
    {
        ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)id);
    }
    m_TextureCache.clear();
}

void ImGuiLayer::SetDarkThemeColors()
{
    auto& style = ImGui::GetStyle();

        // 1. Window & Frame Styling (Professional Rounding & Padding)
    style.WindowRounding = 5.0f;
    style.WindowBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.WindowMinSize = ImVec2(32.0f, 32.0f);

    style.FrameRounding = 3.0f;
    style.FrameBorderSize = 0.0f;
    style.FramePadding = ImVec2(8.0f, 4.0f);

    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.IndentSpacing = 20.0f;

    style.GrabRounding = 3.0f;
    style.GrabMinSize = 10.0f;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);

        // 2. Professional Dark Color Palette
    auto* colors = style.Colors;

        // Backgrounds
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.94f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.10f, 0.94f);

        // Frames
    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.16f, 0.54f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.26f, 0.26f, 0.40f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.26f, 0.67f);

        // Title Bar
    colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.29f, 0.29f, 0.29f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);

        // Menu Bar
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);

        // Scrollbar
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);

        // Buttons
    colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 0.40f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

        // Header/Collapsing
    colors[ImGuiCol_Header] = ImVec4(0.25f, 0.25f, 0.25f, 0.31f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.35f, 0.35f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

        // Separator
    colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);

        // Tabs
    colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.35f, 0.58f, 0.86f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.41f, 0.68f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.07f, 0.10f, 0.15f, 0.97f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.26f, 0.42f, 1.00f);

        // Docking
    colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);

        // Text & Checkmarks
    colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);

        // Slider/Input
    colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.46f, 0.54f, 0.80f, 0.60f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
}
} // namespace Chimera
