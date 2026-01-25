#include "pch.h"
#include "ImGuiLayer.h"
#include "Application.h" 

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

namespace Chimera {

    ImGuiLayer::ImGuiLayer(const std::shared_ptr<VulkanContext>& context)
        : m_Context(context)
    {
    }

    ImGuiLayer::~ImGuiLayer()
    {
    }

    void ImGuiLayer::OnAttach()
    {
        // 1. 创建 Descriptor Pool (ImGui 需要大量描述符)
        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        if (vkCreateDescriptorPool(m_Context->GetDevice(), &pool_info, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui descriptor pool");
        }

        // 2. 初始化 ImGui 上下文
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // 启用 Docking!
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;      // 多视口 (可选，需要更复杂的架构支持)

        // 3. 设置样式
        ImGui::StyleColorsDark();
        SetDarkThemeColors(); // 应用自定义暗色主题

        // 4. 创建 RenderPass 和 Framebuffers
        CreateRenderPass();
        CreateFramebuffers();

        // 5. 初始化 ImGui 后端
        ImGui_ImplGlfw_InitForVulkan(m_Context->GetWindow(), true);
        
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = m_Context->GetInstance();
        init_info.PhysicalDevice = m_Context->GetPhysicalDevice();
        init_info.Device = m_Context->GetDevice();
        init_info.QueueFamily = m_Context->FindQueueFamilies(m_Context->GetPhysicalDevice()).graphicsFamily.value();
        init_info.Queue = m_Context->GetGraphicsQueue();
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = m_DescriptorPool;
        init_info.MinImageCount = Config::MAX_FRAMES_IN_FLIGHT;
        init_info.ImageCount = Config::MAX_FRAMES_IN_FLIGHT;
        init_info.Allocator = nullptr;
        init_info.PipelineInfoMain.RenderPass = m_RenderPass;

        ImGui_ImplVulkan_Init(&init_info);
    }

    void ImGuiLayer::OnDetach()
    {
        vkDeviceWaitIdle(m_Context->GetDevice());

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        for (auto fb : m_Framebuffers) vkDestroyFramebuffer(m_Context->GetDevice(), fb, nullptr);
        vkDestroyRenderPass(m_Context->GetDevice(), m_RenderPass, nullptr);
        vkDestroyDescriptorPool(m_Context->GetDevice(), m_DescriptorPool, nullptr);
    }

    void ImGuiLayer::OnResize(uint32_t width, uint32_t height)
    {
        // 窗口大小改变，销毁旧的 Framebuffer 并重建
        for (auto fb : m_Framebuffers) {
            vkDestroyFramebuffer(m_Context->GetDevice(), fb, nullptr);
        }
        CreateFramebuffers();
    }

    void ImGuiLayer::Begin()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // 开启全屏 DockSpace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
    }

    void ImGuiLayer::OnUIRender()
    {
    }

    void ImGuiLayer::End(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        ImGui::Render();

        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = m_RenderPass;
        info.framebuffer = m_Framebuffers[imageIndex];
        info.renderArea.extent = m_Context->GetSwapChainExtent();
        info.clearValueCount = 0;
        info.pClearValues = nullptr;

        vkCmdBeginRenderPass(commandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        vkCmdEndRenderPass(commandBuffer);
    }

    void ImGuiLayer::CreateRenderPass()
    {
        VkAttachmentDescription attachment = {};
        attachment.format = m_Context->GetSwapChainImageFormat();
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // 保留之前画的内容
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;

        if (vkCreateRenderPass(m_Context->GetDevice(), &info, nullptr, &m_RenderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create ImGui RenderPass");
        }
    }

    void ImGuiLayer::CreateFramebuffers()
    {
        const auto& imageViews = m_Context->GetSwapChainImageViews();
        VkExtent2D extent = m_Context->GetSwapChainExtent();
        
        m_Framebuffers.resize(imageViews.size());

        for (size_t i = 0; i < imageViews.size(); i++) {
            VkImageView attachment[] = { imageViews[i] };
            VkFramebufferCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = m_RenderPass;
            info.attachmentCount = 1;
            info.pAttachments = attachment;
            info.width = extent.width;
            info.height = extent.height;
            info.layers = 1;

            if (vkCreateFramebuffer(m_Context->GetDevice(), &info, nullptr, &m_Framebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create ImGui Framebuffer");
            }
        }
    }

    void ImGuiLayer::SetDarkThemeColors()
    {
        auto& colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

        // Headers
        colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_HeaderActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        
        // Buttons
        colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_ButtonActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

        // Frame BG
        colors[ImGuiCol_FrameBg] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
        colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
        colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
        colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
        colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };

        // Title
        colors[ImGuiCol_TitleBg] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.15f, 0.1505f, 0.151f, 1.0f };
    }
}