#include "GBufferDescription.h"
#include "Lighting.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include <vulkan/vulkan_core.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <set>
#include <limits>
#include <algorithm>
#include <chrono>
#include "JungleApp.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "VulkanHelper.h"

void JungleApp::initVulkan(const std::string& sceneName, bool recompileShaders) {
    device.initInstance();
    createSurface();
    device.initDeviceForSurface(surface);

    swapchain = std::make_unique<Swapchain>(window, surface, &device);
    setupRenderStageScene(sceneName, recompileShaders);

    lighting = std::make_unique<DeferredLighting>(&device, swapchain.get());
    lighting->setup(recompileShaders, &scene, mvpSetLayout);

    tonemap = std::make_unique<Tonemap>(&device, swapchain.get());
    tonemap->setupRenderStageTonemap(recompileShaders);

    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
}

void JungleApp::setupRenderStageScene(const std::string& sceneName, bool recompileShaders) {
    setupScene(sceneName);
    createScenePass();
    createMVPSetLayout();
    scene.createPipelines(sceneRPass, mvpSetLayout, recompileShaders);
    setupGBuffer();
}

void JungleApp::setupGBuffer() {
    // The layout of the gBuffer needs to match GBufferTargets
    gBuffer.init(&device, MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < GBufferTarget::NumAttachments; i++) {
        if (i == GBufferTarget::Depth) {
            gBuffer.addAttachment(swapchain->swapChainExtent, swapchain->chooseDepthFormat(),
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        } else {
            auto fmt = getGBufferAttachmentFormat(swapchain.get(), (GBufferTarget)i);
            gBuffer.addAttachment(swapchain->swapChainExtent, fmt,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    gBuffer.createFramebuffers(sceneRPass, swapchain->swapChainExtent);
}

void JungleApp::mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        drawFrame();
    }

    vkDeviceWaitIdle(device.device);
}

void JungleApp::drawImGUI() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    if (ImGui::Begin("Settings")) {
        forceReloadShaders = ImGui::Button("Reload Shaders");
        ImGui::Checkbox("Show Dear ImGui Demo", &showDemoWindow);
        ImGui::Checkbox("Show Metrics", &showMetricsWindow);
        if (ImGui::CollapsingHeader("Music Settings")) {
            if (ImGui::Checkbox("Enable Music", &playMusic)) {
                if (playMusic) mplayer.play();
                else mplayer.pause();
            }
        }
        if (ImGui::CollapsingHeader("Debug Settings")) {
            ImGui::Combo("G-Buffer Visualization", &lighting->debug.compositionMode, "None\0Albedo\0Depth\0Position\0Normal\0\0");
            ImGui::Checkbox("Show Light BBoxes", (bool*)&lighting->debug.showLightBoxes);
            ImGui::SliderFloat("Light bbox log size", &lighting->lightRadiusLog, -5.f, 5.f);
        }
        if (ImGui::CollapsingHeader("Video Settings")) {
            forceRecreateSwapchain = ImGui::Checkbox("VSync", &swapchain->enableVSync);
        }
        if (ImGui::CollapsingHeader("Camera Settings")) {
            ImGui::DragFloatRange2("Clipping Planes", &nearPlane, &farPlane, 0.07f, .01f, 100000.f);
            ImGui::SliderFloat("Camera FOV", &cameraFOVY, 1.f, 179.f);
            ImGui::DragFloat3("Camera PoI", &cameraLookAt.x, 0.01f);
            ImGui::DragFloat3("Camera PoV", &cameraPosition.x, 0.01f);
            ImGui::DragFloat3("Camera Up", &cameraUpVector.x, 0.01f);
        }
        if (ImGui::CollapsingHeader("Scene Settings")) {
            ImGui::Checkbox("Spin", &spinScene);
            ImGui::SliderFloat("Fixed spin", &fixedRotation, 0.0f, 360.0f);
        }
        if (ImGui::CollapsingHeader("Color Settings")) {
            ImGui::SliderFloat("Exposure", &tonemap->exposure, -10, 10);
            ImGui::SliderFloat("Gamma", &tonemap->gamma, 0, 4);
            ImGui::Combo("Tonemapping", &tonemap->tonemappingMode, "None\0Hable\0AgX\0\0");
        }
    }
    ImGui::End();

    if (showMetricsWindow) {
        ImGui::ShowMetricsWindow(&showMetricsWindow);
    }
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    for (auto& [stage, msg] : GraphicsPipeline::errorsFromShaderCompilation) {
        if (ImGui::Begin(stage.c_str())) {
            ImGui::Text("%s", msg.c_str());
        }
        ImGui::End();
    }
}

void JungleApp::drawFrame() {
    auto imageIndex = swapchain->acquireNextImage(sceneRPass);
    if (!imageIndex.has_value()) {
        return;
    }

    if (forceReloadShaders) {
        GraphicsPipeline::errorsFromShaderCompilation.clear();
        recompileShaders();
    }

    drawImGUI();

    updateUniformBuffers(swapchain->currentFrame);

    auto& commandBuffer = commandBuffers[swapchain->currentFrame];

    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0; // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional
    VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo))

    startRenderPass(commandBuffer, swapchain->currentFrame, sceneRPass);
    scene.recordCommandBuffer(commandBuffer, sceneDescriptorSets[swapchain->currentFrame]);
    vkCmdEndRenderPass(commandBuffer);

    lighting->recordCommandBuffer(commandBuffer, sceneDescriptorSets[swapchain->currentFrame], &scene);
    tonemap->recordTonemapCommandBuffer(commandBuffer, swapchain->defaultTarget.framebuffers[*imageIndex]);

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer))

    auto result = swapchain->queuePresent(commandBuffers[swapchain->currentFrame], *imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized || forceRecreateSwapchain) {
        framebufferResized = false;
        forceRecreateSwapchain = false;
        swapchain->recreateSwapChain(tonemap->tonemapRPass);

        gBuffer.destroyAll();
        setupGBuffer();
        scene.createPipelines(sceneRPass, mvpSetLayout, false);
        lighting->handleResize(gBuffer, mvpSetLayout, &scene);
        tonemap->handleResize(lighting->compositedLight);
    } else {
        VK_CHECK_RESULT(result)
    }
}

void JungleApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Bioluminescent Jungle", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void JungleApp::initImGui() {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImGui::StyleColorsDark();

    // Create a separate descriptor pool for ImGui.
    // Ours is sized big enough to hold the descriptor sets for the scene itself.
    // ImGui might need more. Even though most Vulkan implementations try to allocate nonetheless, my
    // AMD iGPU does not. Source:
    // https://github.com/ocornut/imgui/blob/master/examples/example_glfw_vulkan/main.cpp
    VkDescriptorPoolSize poolSizes[] =
    {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = (uint32_t)IM_ARRAYSIZE(poolSizes);
    poolInfo.pPoolSizes = poolSizes;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &imguiDescriptorPool));

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = device.instance;
    init_info.PhysicalDevice = device.physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = device.chosenQueues.graphicsFamily.value();
    init_info.Queue = device.graphicsQueue;
    //init_info.PipelineCache = YOUR_PIPELINE_CACHE;
    init_info.DescriptorPool = imguiDescriptorPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    // init_info.Allocator = YOUR_ALLOCATOR;
    // init_info.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&init_info, tonemap->tonemapRPass);

    VkCommandPool command_pool = device.commandPool;
    VkCommandBuffer command_buffer = commandBuffers[0];

    VK_CHECK_RESULT(vkResetCommandPool(device, command_pool, 0))
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK_RESULT(vkBeginCommandBuffer(command_buffer, &begin_info))

    ImGui_ImplVulkan_CreateFontsTexture(command_buffer);

    VkSubmitInfo end_info = {};
    end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    end_info.commandBufferCount = 1;
    end_info.pCommandBuffers = &command_buffer;
    VK_CHECK_RESULT(vkEndCommandBuffer(command_buffer))
    VK_CHECK_RESULT(vkQueueSubmit(device.graphicsQueue, 1, &end_info, VK_NULL_HANDLE))

    VK_CHECK_RESULT(vkDeviceWaitIdle(device))
    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void JungleApp::createSurface() {
    VK_CHECK_RESULT(glfwCreateWindowSurface(device.instance, window, nullptr, &surface))
}

void JungleApp::recompileShaders() {
    vkDeviceWaitIdle(device);

    scene.createPipelines(sceneRPass, mvpSetLayout, true);
    lighting->createPipeline(true, mvpSetLayout, &scene);
    tonemap->createTonemapPipeline(true);
}

void JungleApp::createScenePass() {
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> colorAttachments;
    VkAttachmentReference depthAttachmentRef;

    for (int i = 0; i < GBufferTarget::NumAttachments; i++) {
        auto fmt = getGBufferAttachmentFormat(swapchain.get(), (GBufferTarget)i);
        VkAttachmentDescription attachment{};
        attachment.format = fmt;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments.push_back(attachment);

        if (i == GBufferTarget::Depth) {
            depthAttachmentRef.attachment = i;
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        } else {
            VkAttachmentReference ref{};
            ref.attachment = i;
            ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachments.push_back(ref);
        }
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = colorAttachments.size();
    subpass.pColorAttachments = colorAttachments.data();
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // We need to first transition the attachments to ATTACHMENT_WRITE, then transition back to READ for the
    // next stages
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = dependencies.size();
    renderPassInfo.pDependencies = dependencies.data();

    VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &sceneRPass))
}

void JungleApp::createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = device.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();
    VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()))
}

void JungleApp::startRenderPass(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkRenderPass renderPass) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = gBuffer.framebuffers[currentFrame];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain->swapChainExtent;

    std::array<VkClearValue, GBufferTarget::NumAttachments> clearValues{};
    for (int i = 0; i < GBufferTarget::NumAttachments; i++) {
        if (i == GBufferTarget::Depth) {
            clearValues[i].depthStencil = {1.0f, 0};
        } else {
            clearValues[i].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        }
    }

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void JungleApp::createMVPSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &mvpSetLayout))
}

void JungleApp::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    mvpUBO.allocate(&device, bufferSize, MAX_FRAMES_IN_FLIGHT);
    lighting->setupBuffers();
    tonemap->setupBuffers();
}

void JungleApp::updateUniformBuffers(uint32_t currentImage) {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
    float rotation = spinScene ? time * glm::radians(90.0f) : glm::radians(fixedRotation);

    UniformBufferObject ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(cameraPosition, cameraLookAt, cameraUpVector);
    ubo.proj = glm::perspective(glm::radians(cameraFOVY),
        (float) swapchain->swapChainExtent.width / (float) swapchain->swapChainExtent.height, nearPlane, farPlane);
    ubo.proj[1][1] *= -1;  // because GLM generates OpenGL projections

    mvpUBO.update(&ubo, sizeof(ubo), currentImage);
    lighting->updateBuffers(ubo.proj * ubo.view, cameraPosition, cameraUpVector);

    // TODO: is there a better way to integrate this somehow? Too lazy to skip the tonemapping render pass completely.
    if (lighting->debug.compositionMode != 0) {
        tonemap->tonemappingMode = 0;
    }
    tonemap->updateBuffers();
}

void JungleApp::createDescriptorPool() {

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    std::vector<RequiredDescriptors> requirements = {
        scene.getNumDescriptors(), lighting->getNumDescriptors(), tonemap->getNumDescriptors()
    };

    // Descriptors required in JungleApp itself
    requirements.push_back({
        .requireUniformBuffers = MAX_FRAMES_IN_FLIGHT,
        .requireSamplers = 0,
    });

    for (auto& req : requirements) {
        poolSizes[0].descriptorCount += req.requireUniformBuffers;
        poolSizes[1].descriptorCount += req.requireSamplers;
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = poolSizes[0].descriptorCount + poolSizes[1].descriptorCount;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool))
}

void JungleApp::createDescriptorSets() {
    sceneDescriptorSets = VulkanHelper::createDescriptorSetsFromLayout(device, descriptorPool,
        mvpSetLayout, MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mvpUBO.buffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = sceneDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        descriptorWrite.pImageInfo = nullptr; // Optional
        descriptorWrite.pTexelBufferView = nullptr; // Optional
        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }

    scene.setupDescriptorSets(descriptorPool);
    lighting->createDescriptorSets(descriptorPool, gBuffer);
    tonemap->createDescriptorSets(descriptorPool, lighting->compositedLight);
}

void JungleApp::cleanup() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    swapchain.reset();

    mvpUBO.destroy(&device);
    lighting.reset();
    tonemap.reset();
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr);

    vkDestroyDescriptorSetLayout(device, mvpSetLayout, nullptr);

    scene.destroyAll();
    gBuffer.destroyAll();

    vkDestroyRenderPass(device, sceneRPass, nullptr);
    vkDestroySurfaceKHR(device.instance, surface, nullptr);
    device.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void JungleApp::setupScene(const std::string& sceneName) {
    scene = Scene(&device, swapchain.get(), sceneName);
    scene.setupBuffers();
    scene.setupTextures();
    scene.computeCameraPos(cameraLookAt, cameraPosition, cameraFOVY);
}
