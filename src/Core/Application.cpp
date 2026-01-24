#include "pch.h"
#include "Application.h"
#include "Buffer.h"
#include "Image.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "Random.h"

namespace Chimera {

    const uint32_t WIDTH = 800;
    const uint32_t HEIGHT = 600;
    const std::string MODEL_PATH = "assets/models/viking_room.obj";
    const std::string TEXTURE_PATH = "assets/textures/viking_room.png";
    const int MAX_FRAMES_IN_FLIGHT = 3;

    static std::vector<char> readFile(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("failed to open file: " + filename);
        }
        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    Application::Application()
    {
        initWindow();
        initVulkan();
    }

    Application::~Application()
    {
        cleanup();
    }

    void Application::Run()
    {
        mainLoop();
    }

    void Application::initWindow()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Chimera Engine", nullptr, nullptr);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
        glfwSetKeyCallback(window, keyCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
        glfwSetCursorPosCallback(window, cursorPosCallback);
    }

    void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (app->m_CameraController)
            app->m_CameraController->OnKey(key, scancode, action, mods);
    }

    void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (app->m_CameraController)
            app->m_CameraController->OnMouseButton(button, action, mods);
    }

    void Application::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
    {
        auto app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        if (app->m_CameraController)
            app->m_CameraController->OnCursorPos(xpos, ypos);
    }

    void Application::initVulkan()
    {
        m_Context = std::make_shared<VulkanContext>(window);
        m_Renderer = std::make_shared<Renderer>(m_Context);
        m_ResourceManager = std::make_unique<ResourceManager>(m_Context);

        // Swapchain is created in VulkanContext constructor
        
        createDescriptorSetLayout();
        createGraphicsPipeline();
        // createCommandPool(); // Managed by Context
        createColorResources();
        createDepthResources();
        
        textureImage = m_ResourceManager->LoadTexture(TEXTURE_PATH);

        m_Scene = std::make_shared<Scene>(m_Context);
        m_Scene->LoadModel(MODEL_PATH);
        
        m_CameraController = std::make_unique<CameraController>();
        m_CameraController->SetCamera(&m_Scene->GetCamera());

        createTopLevelAS();

        createStorageImage();
        createAccumulationImage();
        createRayTracingDescriptorSetLayout();
        createRayTracingDescriptorSets();
        createRayTracingPipeline();
        createShaderBindingTable();

        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
    }

    void Application::mainLoop()
    {
        while (!glfwWindowShouldClose(window))
        {
            float time = (float)glfwGetTime();
            float timestep = time - m_LastFrameTime;
            m_LastFrameTime = time;

            glfwPollEvents();

            if (m_CameraController)
                m_CameraController->OnUpdate(timestep);

            for (auto& layer : m_LayerStack)
            {
                layer->OnUpdate(timestep);
            }

            for (auto& layer : m_LayerStack)
            {
                layer->OnUIRender();
            }

            drawFrame();
        }
        vkDeviceWaitIdle(m_Context->GetDevice());
    }

    void Application::PushLayer(const std::shared_ptr<Layer>& layer)
    {
        m_LayerStack.push_back(layer);
        layer->OnAttach();
    }

    void Application::cleanup()
    {
        cleanupSwapChain();

        // RT Pipeline cleanup
        vkDestroyPipeline(m_Context->GetDevice(), rayTracingPipeline, nullptr);
        vkDestroyPipelineLayout(m_Context->GetDevice(), rayTracingPipelineLayout, nullptr);

        vkDestroyDescriptorPool(m_Context->GetDevice(), rtDescriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_Context->GetDevice(), rtDescriptorSetLayout, nullptr);

        // AS cleanup
        vkDestroyAccelerationStructureKHR(m_Context->GetDevice(), topLevelAS, nullptr);

        // Descriptors cleanup
        vkDestroyDescriptorPool(m_Context->GetDevice(), descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_Context->GetDevice(), descriptorSetLayout, nullptr);

        // Graphics Pipeline cleanup
        vkDestroyPipeline(m_Context->GetDevice(), graphicsPipeline, nullptr);
        vkDestroyPipelineLayout(m_Context->GetDevice(), pipelineLayout, nullptr);
        
        // Sync Objects cleanup handled by Renderer destructor

        // vkDestroyCommandPool(m_Context->GetDevice(), commandPool, nullptr); // Managed by Context

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void Application::createDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    void Application::createGraphicsPipeline()
    {
        auto vertShaderCode = readFile("shaders/shader.vert.spv");
        auto fragShaderCode = readFile("shaders/shader.frag.spv");

        VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
        VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)m_Context->GetSwapChainExtent().width;
        viewport.height = (float)m_Context->GetSwapChainExtent().height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_Context->GetSwapChainExtent();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_TRUE;
        multisampling.rasterizationSamples = m_Context->GetMSAASamples();
        multisampling.minSampleShading = 0.2f;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

        if (vkCreatePipelineLayout(m_Context->GetDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create pipeline layout!");
        }

        VkFormat colorFormat = m_Context->GetSwapChainImageFormat();
        VkFormat depthFormat = findDepthFormat();

        VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{};
        pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipelineRenderingCreateInfo.colorAttachmentCount = 1;
        pipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
        pipelineRenderingCreateInfo.depthAttachmentFormat = depthFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &pipelineRenderingCreateInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(m_Context->GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create graphics pipeline!");
        }

        vkDestroyShaderModule(m_Context->GetDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Context->GetDevice(), vertShaderModule, nullptr);
    }



    VkShaderModule Application::createShaderModule(const std::vector<char>& code)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_Context->GetDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create shader module!");
        }
        return shaderModule;
    }

    VkCommandBuffer Application::beginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_Context->GetCommandPool();
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(m_Context->GetDevice(), &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void Application::endSingleTimeCommands(VkCommandBuffer commandBuffer)
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(m_Context->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_Context->GetGraphicsQueue());

        vkFreeCommandBuffers(m_Context->GetDevice(), m_Context->GetCommandPool(), 1, &commandBuffer);
    }

    VkFormat Application::findDepthFormat()
    {
        return m_Context->findSupportedFormat(
            { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }

    void Application::cleanupSwapChain()
    {
        // Image objects (colorImage, depthImage) are managed by std::unique_ptr
        // and will be automatically destroyed when reset or when Application is destroyed.
        // However, we want to explicitly release them when resizing the swapchain
        // so they can be recreated with new dimensions.
        colorImage.reset();
        depthImage.reset();
        
        // Swapchain and image views are cleaned up by VulkanContext
    }

    void Application::createColorResources()
    {
        VkFormat colorFormat = m_Context->GetSwapChainImageFormat();
        
        colorImage = std::make_unique<Image>(
            m_Context->GetAllocator(),
            m_Context->GetDevice(),
            m_Context->GetSwapChainExtent().width,
            m_Context->GetSwapChainExtent().height,
            colorFormat,
            VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            m_Context->GetMSAASamples()
        );
    }

    void Application::createDepthResources()
    {
        VkFormat depthFormat = findDepthFormat();
        
        depthImage = std::make_unique<Image>(
            m_Context->GetAllocator(),
            m_Context->GetDevice(),
            m_Context->GetSwapChainExtent().width,
            m_Context->GetSwapChainExtent().height,
            depthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            1,
            m_Context->GetMSAASamples()
        );

        transitionImageLayout(depthImage->GetImage(), depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
    }

    void Application::createStorageImage()
    {
        storageImage = std::make_unique<Image>(
            m_Context->GetAllocator(),
            m_Context->GetDevice(),
            m_Context->GetSwapChainExtent().width,
            m_Context->GetSwapChainExtent().height,
            storageImageFormat,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        transitionImageLayout(storageImage->GetImage(), storageImageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1);
    }

    void Application::createAccumulationImage()
    {
        accumulationImage = std::make_unique<Image>(
            m_Context->GetAllocator(),
            m_Context->GetDevice(),
            m_Context->GetSwapChainExtent().width,
            m_Context->GetSwapChainExtent().height,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        transitionImageLayout(accumulationImage->GetImage(), VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1);
    }



    void Application::createRayTracingDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding asLayoutBinding{};
        asLayoutBinding.binding = 0;
        asLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        asLayoutBinding.descriptorCount = 1;
        asLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        VkDescriptorSetLayoutBinding storageImageBinding{};
        storageImageBinding.binding = 1;
        storageImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImageBinding.descriptorCount = 1;
        storageImageBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutBinding accumImageBinding{};
        accumImageBinding.binding = 2;
        accumImageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        accumImageBinding.descriptorCount = 1;
        accumImageBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

        VkDescriptorSetLayoutBinding vertexBufferBinding{};
        vertexBufferBinding.binding = 3;
        vertexBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        vertexBufferBinding.descriptorCount = 1;
        vertexBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        VkDescriptorSetLayoutBinding indexBufferBinding{};
        indexBufferBinding.binding = 4;
        indexBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        indexBufferBinding.descriptorCount = 1;
        indexBufferBinding.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

        std::array<VkDescriptorSetLayoutBinding, 5> bindings = { asLayoutBinding, storageImageBinding, accumImageBinding, vertexBufferBinding, indexBufferBinding };

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_Context->GetDevice(), &layoutInfo, nullptr, &rtDescriptorSetLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create RT descriptor set layout!");
        }
    }

    void Application::createRayTracingDescriptorSets()
    {
        std::array<VkDescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * 2;
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[2].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * 2;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        if (vkCreateDescriptorPool(m_Context->GetDevice(), &poolInfo, nullptr, &rtDescriptorPool) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create RT descriptor pool!");
        }

        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, rtDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = rtDescriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        allocInfo.pSetLayouts = layouts.data();

        rtDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateDescriptorSets(m_Context->GetDevice(), &allocInfo, rtDescriptorSets.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate RT descriptor sets!");
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) 
        {
            VkWriteDescriptorSetAccelerationStructureKHR descriptorAS{};
            descriptorAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            descriptorAS.accelerationStructureCount = 1;
            descriptorAS.pAccelerationStructures = &topLevelAS;

            VkWriteDescriptorSet asWrite{};
            asWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            asWrite.pNext = &descriptorAS;
            asWrite.dstSet = rtDescriptorSets[i];
            asWrite.dstBinding = 0;
            asWrite.descriptorCount = 1;
            asWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

            VkDescriptorImageInfo storageImageInfo{};
            storageImageInfo.imageView = storageImage->GetView();
            storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet storageImageWrite{};
            storageImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            storageImageWrite.dstSet = rtDescriptorSets[i];
            storageImageWrite.dstBinding = 1;
            storageImageWrite.descriptorCount = 1;
            storageImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            storageImageWrite.pImageInfo = &storageImageInfo;

            VkDescriptorImageInfo accumImageInfo{};
            accumImageInfo.imageView = accumulationImage->GetView();
            accumImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkWriteDescriptorSet accumImageWrite{};
            accumImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            accumImageWrite.dstSet = rtDescriptorSets[i];
            accumImageWrite.dstBinding = 2;
            accumImageWrite.descriptorCount = 1;
            accumImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            accumImageWrite.pImageInfo = &accumImageInfo;

            VkDescriptorBufferInfo vertexBufferInfo{};
            vertexBufferInfo.buffer = m_Scene->GetVertexBuffer()->GetBuffer();
            vertexBufferInfo.offset = 0;
            vertexBufferInfo.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet vertexWrite{};
            vertexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            vertexWrite.dstSet = rtDescriptorSets[i];
            vertexWrite.dstBinding = 3;
            vertexWrite.descriptorCount = 1;
            vertexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            vertexWrite.pBufferInfo = &vertexBufferInfo;

            VkDescriptorBufferInfo indexBufferInfo{};
            indexBufferInfo.buffer = m_Scene->GetIndexBuffer()->GetBuffer();
            indexBufferInfo.offset = 0;
            indexBufferInfo.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet indexWrite{};
            indexWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            indexWrite.dstSet = rtDescriptorSets[i];
            indexWrite.dstBinding = 4;
            indexWrite.descriptorCount = 1;
            indexWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            indexWrite.pBufferInfo = &indexBufferInfo;

            std::vector<VkWriteDescriptorSet> descriptorWrites = { asWrite, storageImageWrite, accumImageWrite, vertexWrite, indexWrite };
            vkUpdateDescriptorSets(m_Context->GetDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }
    }

    void Application::createRayTracingPipeline()
    {
        auto rgenShaderCode = readFile("shaders/raygen.rgen.spv");
        auto missShaderCode = readFile("shaders/miss.rmiss.spv");
        auto chitShaderCode = readFile("shaders/closesthit.rchit.spv");

        VkShaderModule rgenModule = createShaderModule(rgenShaderCode);
        VkShaderModule missModule = createShaderModule(missShaderCode);
        VkShaderModule chitModule = createShaderModule(chitShaderCode);

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages(3);
        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        shaderStages[0].module = rgenModule;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        shaderStages[1].module = missModule;
        shaderStages[1].pName = "main";

        shaderStages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[2].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        shaderStages[2].module = chitModule;
        shaderStages[2].pName = "main";

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups(3);
        shaderGroups[0].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroups[0].generalShader = 0;
        shaderGroups[0].closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[0].anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[0].intersectionShader = VK_SHADER_UNUSED_KHR;

        shaderGroups[1].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        shaderGroups[1].generalShader = 1;
        shaderGroups[1].closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[1].anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[1].intersectionShader = VK_SHADER_UNUSED_KHR;

        shaderGroups[2].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        shaderGroups[2].generalShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[2].closestHitShader = 2;
        shaderGroups[2].anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

        std::vector<VkDescriptorSetLayout> setLayouts = { rtDescriptorSetLayout, descriptorSetLayout };
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        pipelineLayoutInfo.pSetLayouts = setLayouts.data();

        if (vkCreatePipelineLayout(m_Context->GetDevice(), &pipelineLayoutInfo, nullptr, &rayTracingPipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create RT pipeline layout!");
        }

        VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
        pipelineInfo.pGroups = shaderGroups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = 2;
        pipelineInfo.layout = rayTracingPipelineLayout;

        if (vkCreateRayTracingPipelinesKHR(m_Context->GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &rayTracingPipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create RT pipeline!");
        }

        vkDestroyShaderModule(m_Context->GetDevice(), rgenModule, nullptr);
        vkDestroyShaderModule(m_Context->GetDevice(), missModule, nullptr);
        vkDestroyShaderModule(m_Context->GetDevice(), chitModule, nullptr);
    }

    void Application::createShaderBindingTable()
    {
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR pipelineProperties{};
        pipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

        VkPhysicalDeviceProperties2 deviceProperties2{};
        deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties2.pNext = &pipelineProperties;
        vkGetPhysicalDeviceProperties2(m_Context->GetPhysicalDevice(), &deviceProperties2);

        uint32_t handleSize = pipelineProperties.shaderGroupHandleSize;
        uint32_t handleAlignment = pipelineProperties.shaderGroupHandleAlignment;
        uint32_t baseAlignment = pipelineProperties.shaderGroupBaseAlignment;
        uint32_t handleSizeAligned = align_up(handleSize, handleAlignment);

        uint32_t rgenStride = align_up(handleSizeAligned, baseAlignment);
        uint32_t missStride = align_up(handleSizeAligned, baseAlignment);
        uint32_t hitStride  = align_up(handleSizeAligned, baseAlignment);
        VkDeviceSize sbtSize = rgenStride + missStride + hitStride;

        sbtBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            sbtSize,
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        std::vector<uint8_t> handles(3 * handleSize);
        vkGetRayTracingShaderGroupHandlesKHR(m_Context->GetDevice(), rayTracingPipeline, 0, 3, 3 * handleSize, handles.data());

        uint8_t* pData = reinterpret_cast<uint8_t*>(sbtBuffer->Map());
        VkDeviceAddress sbtAddress = sbtBuffer->GetDeviceAddress();

        memcpy(pData, handles.data(), handleSize);
        rgenRegion.deviceAddress = sbtAddress;
        rgenRegion.stride = rgenStride;
        rgenRegion.size = rgenStride;

        pData += rgenStride;
        memcpy(pData, handles.data() + handleSize, handleSize);
        missRegion.deviceAddress = sbtAddress + rgenStride;
        missRegion.stride = missStride;
        missRegion.size = missStride;

        pData += missStride;
        memcpy(pData, handles.data() + 2 * handleSize, handleSize);
        hitRegion.deviceAddress = sbtAddress + rgenStride + missStride;
        hitRegion.stride = hitStride;
        hitRegion.size = hitStride;

        sbtBuffer->Unmap();
    }

    VkTransformMatrixKHR Application::toVkMatrix(glm::mat4 model)
    {
        glm::mat4 transposed = glm::transpose(model);
        VkTransformMatrixKHR outMatrix;
        memcpy(&outMatrix, &transposed, sizeof(VkTransformMatrixKHR));
        return outMatrix;
    }



    void Application::createTopLevelAS()
    {
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform = toVkMatrix(glm::mat4(1.0f));
        instance.instanceCustomIndex = 0;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = getAccelerationStructureDeviceAddress(m_Scene->GetBLAS());

        Buffer instanceBuffer(
            m_Context->GetAllocator(),
            sizeof(instance),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        instanceBuffer.UploadData(&instance, sizeof(instance));

        VkAccelerationStructureGeometryKHR geometry{};
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.arrayOfPointers = VK_FALSE;
        geometry.geometry.instances.data.deviceAddress = instanceBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        uint32_t primitiveCount = 1;
        VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
        buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(m_Context->GetDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &buildSizesInfo);

        tlasBuffer = std::make_unique<Buffer>(
            m_Context->GetAllocator(),
            buildSizesInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = tlasBuffer->GetBuffer();
        createInfo.size = buildSizesInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        vkCreateAccelerationStructureKHR(m_Context->GetDevice(), &createInfo, nullptr, &topLevelAS);

        Buffer scratchBuffer(
            m_Context->GetAllocator(),
            buildSizesInfo.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );
        
        buildInfo.scratchData.deviceAddress = scratchBuffer.GetDeviceAddress();
        buildInfo.dstAccelerationStructure = topLevelAS;

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = 1;
        rangeInfo.primitiveOffset = 0;
        rangeInfo.firstVertex = 0;
        rangeInfo.transformOffset = 0;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &pRangeInfo);
        endSingleTimeCommands(commandBuffer);
    }

    void Application::createUniformBuffers()
    {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);
        uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            uniformBuffers[i] = std::make_unique<Buffer>(
                m_Context->GetAllocator(),
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );
        }
    }

    void Application::createDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

        vkCreateDescriptorPool(m_Context->GetDevice(), &poolInfo, nullptr, &descriptorPool);
    }

    void Application::createDescriptorSets()
    {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        allocInfo.pSetLayouts = layouts.data();

        descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        vkAllocateDescriptorSets(m_Context->GetDevice(), &allocInfo, descriptorSets.data());
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers[i]->GetBuffer();
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = m_ResourceManager->GetTextureSampler();
            imageInfo.imageView = textureImage->GetView();
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            std::array<VkWriteDescriptorSet, 2> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = descriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &bufferInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = descriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(m_Context->GetDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    uint32_t Application::align_up(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    void Application::updateUniformBuffer(uint32_t currentImage)
    {
        static auto startTime = std::chrono::high_resolution_clock::now();
        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        // Retrieve Camera from Scene
        auto& camera = m_Scene->GetCamera();
        
        // Simple orbiting camera update (to keep the dynamic behavior for now, or we can remove it if Scene has a controller)
        // For now, let's update the scene camera here to match the previous behavior
        // In a real engine, a CameraController system would do this update in OnUpdate().
        // camera.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        // camera.proj = glm::perspective(glm::radians(45.0f), m_Context->GetSwapChainExtent().width / (float)m_Context->GetSwapChainExtent().height, 0.1f, 10.0f);
        // camera.proj[1][1] *= -1;

        UniformBufferObject ubo{};
        ubo.model = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = camera.view;
        ubo.proj = camera.proj;
        
        // Use Scene Light
        auto& light = m_Scene->GetLight();
        // Update light position for animation effect
        light.position = glm::vec4(2.0f * sin(time), 4.0f, 2.0f * cos(time), (float)Random::UInt(0, 100000));
        
        ubo.lightPos = light.position;
        
        uniformBuffers[currentImage]->UploadData(&ubo, sizeof(ubo));
    }

    void Application::drawFrame()
    {
        VkCommandBuffer commandBuffer = m_Renderer->BeginFrame();
        if (commandBuffer == nullptr) return; // SwapChain recreation or minified

        uint32_t frameIndex = m_Renderer->GetCurrentFrameIndex();
        uint32_t imageIndex = m_Renderer->GetCurrentImageIndex();

        updateUniformBuffer(frameIndex);

        recordCommandBuffer(commandBuffer, imageIndex);
        
        m_Renderer->EndFrame();
    }

    void Application::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        // vkBeginCommandBuffer(commandBuffer, &beginInfo); // Handled by Renderer::BeginFrame
        
        const auto& swapChainImages = m_Context->GetSwapChainImages();

        // 1. Ray Tracing Pre-Barrier (Transition Images)
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = storageImage->GetImage();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipeline);
        
        uint32_t currentFrame = m_Renderer->GetCurrentFrameIndex();
        
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipelineLayout, 0, 1, &rtDescriptorSets[currentFrame], 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipelineLayout, 1, 1, &descriptorSets[currentFrame], 0, nullptr);
        vkCmdTraceRaysKHR(commandBuffer, &rgenRegion, &missRegion, &hitRegion, &callRegion, WIDTH, HEIGHT, 1);

        VkImageMemoryBarrier swapChainBarrier{};
        swapChainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapChainBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        swapChainBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapChainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapChainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapChainBarrier.image = swapChainImages[imageIndex];
        swapChainBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swapChainBarrier.subresourceRange.baseMipLevel = 0;
        swapChainBarrier.subresourceRange.levelCount = 1;
        swapChainBarrier.subresourceRange.baseArrayLayer = 0;
        swapChainBarrier.subresourceRange.layerCount = 1;
        swapChainBarrier.srcAccessMask = 0;
        swapChainBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        VkImageMemoryBarrier storageBarrier{};
        storageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        storageBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        storageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        storageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        storageBarrier.image = storageImage->GetImage();
        storageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        storageBarrier.subresourceRange.baseMipLevel = 0;
        storageBarrier.subresourceRange.levelCount = 1;
        storageBarrier.subresourceRange.baseArrayLayer = 0;
        storageBarrier.subresourceRange.layerCount = 1;
        storageBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        storageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapChainBarrier);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &storageBarrier);

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.srcOffset = { 0, 0, 0 };
        copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copyRegion.dstOffset = { 0, 0, 0 };
        copyRegion.extent = { WIDTH, HEIGHT, 1 };
        vkCmdCopyImage(commandBuffer, storageImage->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        VkImageMemoryBarrier presentBarrier{};
        presentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        presentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        presentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        presentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        presentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        presentBarrier.image = swapChainImages[imageIndex];
        presentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        presentBarrier.subresourceRange.baseMipLevel = 0;
        presentBarrier.subresourceRange.levelCount = 1;
        presentBarrier.subresourceRange.baseArrayLayer = 0;
        presentBarrier.subresourceRange.layerCount = 1;
        presentBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        presentBarrier.dstAccessMask = 0;

        VkImageMemoryBarrier storageResetBarrier{};
        storageResetBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        storageResetBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        storageResetBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageResetBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        storageResetBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        storageResetBarrier.image = storageImage->GetImage();
        storageResetBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        storageResetBarrier.subresourceRange.baseMipLevel = 0;
        storageResetBarrier.subresourceRange.levelCount = 1;
        storageResetBarrier.subresourceRange.baseArrayLayer = 0;
        storageResetBarrier.subresourceRange.layerCount = 1;
        storageResetBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        storageResetBarrier.dstAccessMask = 0;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &presentBarrier);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &storageResetBarrier);

        // vkEndCommandBuffer(commandBuffer); // Handled by Renderer::EndFrame
    }

    void Application::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage, destinationStage;
        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT)
            {
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; 
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        endSingleTimeCommands(commandBuffer);
    }

    void Application::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();
        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
        endSingleTimeCommands(commandBuffer);
    }

    uint64_t Application::getAccelerationStructureDeviceAddress(VkAccelerationStructureKHR as)
    {
        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        addressInfo.accelerationStructure = as;
        return vkGetAccelerationStructureDeviceAddressKHR(m_Context->GetDevice(), &addressInfo);
    }
}
