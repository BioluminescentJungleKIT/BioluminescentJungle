// Unless noted otherwise, content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.
//
// Parts of this file are licensed under MIT.
// Copyright (c) 2014-2024 Omar Cornut

#include "GBufferDescription.h"
#include "Lighting.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include <thread>
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include "JungleApp.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "VulkanHelper.h"

void JungleApp::initVulkan(const std::string &sceneName, bool recompileShaders) {
    device.initInstance();
    createSurface();
    device.initDeviceForSurface(surface);

    swapchain = std::make_unique<Swapchain>(window, surface, &device);
    setupRenderStageScene(sceneName, recompileShaders);

    lighting = std::make_unique<DeferredLighting>(&device, swapchain.get());
    lighting->setup(recompileShaders, &scene, mvpSetLayout);

    this->groundBVH = std::make_unique<BVH>(&device, &scene, "Ground");

    postprocessing = std::make_unique<PostProcessing>(&device, swapchain.get());
    lighting->fogAbsorption = &postprocessing->getFogPointer()->absorption;
    postprocessing->setupRenderStages(recompileShaders);

    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
}

void JungleApp::setupRenderStageScene(const std::string &sceneName, bool recompileShaders) {
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
            gBuffer.addAttachment(swapchain->renderSize(), swapchain->chooseDepthFormat(),
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_DEPTH_BIT);
        } else {
            auto fmt = getGBufferAttachmentFormat(swapchain.get(), (GBufferTarget) i);
            gBuffer.addAttachment(swapchain->renderSize(), fmt,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    gBuffer.createFramebuffers(sceneRPass, swapchain->renderSize());
}

void JungleApp::mainLoop() {
    using namespace std::chrono;

    while (!glfwWindowShouldClose(window)) {
        auto startFrame = system_clock::now();
        glfwPollEvents();
        cameraMotion();
        drawFrame();

        if (Swapchain::rateLimit > 0) {
            int elapsed = duration_cast<milliseconds>(system_clock::now() - startFrame).count();
            int maxNs = 1e6 / Swapchain::rateLimit;
            maxNs = std::max(0, 1'000 / Swapchain::rateLimit - elapsed);
            std::this_thread::sleep_for(milliseconds(maxNs));
        }
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
            ImGui::Combo("G-Buffer Visualization", &lighting->debug.compositionMode,
                         "None\0Albedo\0Depth\0Position\0Normal\0Motion\0SSR Region\0Emissive Color\0Point Lights\0\0");
            ImGui::Combo("Lighting mode", &lighting->computeLightAlgo,
                         "ReSTIR\0Bruteforce\0BVH only\0\0");
            ImGui::SliderFloat("ReSTIR Temporal Factor", &lighting->restirTemporalFactor, 0.0f, 500.0f);
            ImGui::SliderInt("ReSTIR Spatial Radius", &lighting->restirSpatialRadius, 0, 50);
            ImGui::SliderInt("ReSTIR Spatial Neighbors", &lighting->restirSpatialNeighbors, 0, 100);
            ImGui::SliderInt("ReSTIR Fresh Samples per Pixel", &lighting->restirInitialSamples, 1, 1024);
            ImGui::SliderFloat("ReSTIR Light Grid Search Radius", &lighting->restirLightGridRadius, 0.1, 20);
            ImGui::SliderFloat("ReSTIR Light Grid Search Alpha", &lighting->restirLightGridSearchAlpha, 0.01, 4.0);
            ImGui::Combo("ReSTIR Sampling Mode", &lighting->restirSamplingMode,
                         "Weighted Light Grid\0Uniform Light Grid\0Uniform\0\0");
            ImGui::SliderFloat("ReSTIR Point Light Relative Importance", &lighting->restirPointLightImportance, 0.0, 1.0);

            ImGui::SliderFloat("Butterfly Luminance", &lighting->pointLightIntensityMultiplier, 0.0, 1000.0);
            if (ImGui::Checkbox("Strong Butterfly Illumination", &illuminationViaButterflies)) {
                if (illuminationViaButterflies) {
                    lighting->restirTemporalFactor = 10.0;
                    lighting->restirInitialSamples = 128;
                    lighting->pointLightIntensityMultiplier = 250;
                } else {
                    lighting->restirTemporalFactor = 50.0;
                    lighting->restirInitialSamples = 32;
                    lighting->pointLightIntensityMultiplier = 1;
                }
            }

            ImGui::Checkbox("Show Light BBoxes", (bool *) &lighting->debug.showLightBoxes);
            ImGui::SliderFloat("Light bbox log size", &lighting->lightRadiusLog, -5.f, 5.f);
        }
        if (ImGui::CollapsingHeader("Video Settings")) {
            switchFullscreen = ImGui::Checkbox("Fullscreen", &fullscreen);
            forceRecreateSwapchain = ImGui::Checkbox("VSync", &swapchain->enableVSync);
            ImGui::Checkbox("Enable TAA Jitter", &doJitter);
            ImGui::SliderFloat("TAA alpha", &postprocessing->getTAAPointer()->alpha, 0.f, 1.f);
            ImGui::Combo("TAA Neighborhood Clamping", &postprocessing->getTAAPointer()->mode,
                         "Off\0Min-Max\0Moment-Based\0\0");

            ImGui::SliderInt("Denoiser iterations",
                &lighting->getDenoiser()->iterationCount, 0, 20);
            ImGui::SliderFloat("Denoiser Albedo Sigma",
                &lighting->getDenoiser()->ubo.albedoSigma, 0.001, 5.0);
            ImGui::SliderFloat("Denoiser Normal Sigma",
                &lighting->getDenoiser()->ubo.normalSigma, 0.001, 5.0);
            ImGui::SliderFloat("Denoiser Position Sigma",
                &lighting->getDenoiser()->ubo.positionSigma, 0.001, 5.0);
            ImGui::Checkbox("Remove albedo", &lighting->getDenoiser()->ignoreAlbedo);
        }
        if (ImGui::CollapsingHeader("Camera Settings")) {
            ImGui::DragFloatRange2("Clipping Planes", &nearPlane, &farPlane, 0.07f, .01f, 100000.f);
            ImGui::SliderFloat("Camera FOV", &cameraFOVY, 1.f, 179.f);
            ImGui::DragFloat3("Camera PoI", &cameraFinalLookAt.x, 0.01f);
            ImGui::DragFloat3("Camera PoV", &cameraFinalPosition.x, 0.01f);
            ImGui::DragFloat3("Camera Up", &cameraUpVector.x, 0.01f);
            ImGui::Checkbox("Force constant camera height", &cameraFixedHeight);
            ImGui::SliderFloat("Height above ground", &cameraHeightAboveGround, 0.0f, 10.0f);
            ImGui::SliderFloat("Camera Teleport Speed", &cameraMovementSpeed, 0.0f, 50.0f);
            ImGui::Checkbox("Invert mouse motion", &invertMouse);
            scene.cameraButtons(cameraFinalLookAt, cameraFinalPosition, cameraUpVector, cameraFOVY, nearPlane, farPlane);
        }
        if (ImGui::CollapsingHeader("Scene Settings")) {
            ImGui::Checkbox("Time", &doMotion);
            ImGui::Checkbox("Spin", &spinScene);
            ImGui::SliderFloat("Fixed spin", &fixedRotation, 0.0f, 360.0f);
            ImGui::SliderFloat("SSR strength", &postprocessing->getFogPointer()->ssrStrength, 0, 1);
            ImGui::SliderFloat("SSR Edge Smoothing", &postprocessing->getFogPointer()->ssrEdgeSmoothing, 0, 4);
            ImGui::SliderFloat("SSR Hit Threshold", &postprocessing->getFogPointer()->ssrHitThreshold, 0, 0.01, "%.6f");
            ImGui::SliderInt("SSR Raymarch Steps", &postprocessing->getFogPointer()->ssrRaySteps, 1, 1000);
        }
        if (ImGui::CollapsingHeader("Fog Settings")) {
            ImGui::ColorEdit3("Color", &postprocessing->getFogPointer()->color.r);
            ImGui::SliderFloat("Brightness", &postprocessing->getFogPointer()->brightness, 0.f, 10.f);
            ImGui::SliderFloat("Ambient Effect", &postprocessing->getFogPointer()->ambientFactor, 0.f, 10.f);
            ImGui::SliderFloat("Absorption Coefficient", &postprocessing->getFogPointer()->absorption, 0.f, 10.f);
            ImGui::SliderFloat("Scatter Factor", &lighting->scatterStrength, 0.f, 1.f);
            ImGui::SliderFloat("Bleeding", &lighting->lightBleed, 0.f, 3.f);
        }
        if (ImGui::CollapsingHeader("Color Settings")) {
            ImGui::SliderFloat("Exposure", &postprocessing->getTonemappingPointer()->exposure, -10, 10);
            ImGui::SliderFloat("Gamma", &postprocessing->getTonemappingPointer()->gamma, 0, 4);
            ImGui::Combo("Tonemapping", &postprocessing->getTonemappingPointer()->tonemappingMode,
                         "None\0Hable\0AgX\0\0");
        }
        scene.drawImGUIMaterialSettings();
    }
    ImGui::End();

    if (showMetricsWindow) {
        ImGui::ShowMetricsWindow(&showMetricsWindow);
    }
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    for (auto &[stage, msg]: GraphicsPipeline::errorsFromShaderCompilation) {
        if (ImGui::Begin(stage.c_str())) {
            ImGui::Text("%s", msg.c_str());
        }
        ImGui::End();
    }
}

void JungleApp::drawFrame() {
    handleMotion();

    handleHeight();

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

    auto &commandBuffer = commandBuffers[swapchain->currentFrame];

    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0; // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional
    VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo))

    scene.recordCommandBufferCompute(commandBuffer, cameraPosition);
    startRenderPass(commandBuffer, swapchain->currentFrame, sceneRPass);
    scene.recordCommandBufferDraw(commandBuffer, sceneDescriptorSets[swapchain->currentFrame]);
    vkCmdEndRenderPass(commandBuffer);

    lighting->recordCommandBuffer(commandBuffer, sceneDescriptorSets[swapchain->currentFrame], &scene);
    postprocessing->recordCommandBuffer(commandBuffer,
        swapchain->defaultTarget.framebuffers.begin()->second[*imageIndex]);

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer))


    if (switchFullscreen) {
        if (fullscreen) {
            auto monitor = glfwGetPrimaryMonitor();
            auto mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window, glfwGetPrimaryMonitor(), 0, 0, mode->width, mode->height, mode->refreshRate);
        } else {
            glfwSetWindowMonitor(window, NULL, 0, 0, WIDTH, HEIGHT, GLFW_DONT_CARE);
        }
        switchFullscreen = false;
        framebufferResized = true;
    }

    auto result = swapchain->queuePresent(commandBuffers[swapchain->currentFrame], *imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized ||
        forceRecreateSwapchain) {
        framebufferResized = false;
        forceRecreateSwapchain = false;
        swapchain->recreateSwapChain(postprocessing->getFinalRenderPass());

        gBuffer.destroyAll();
        setupGBuffer();
        scene.createPipelines(sceneRPass, mvpSetLayout, false);
        lighting->handleResize(gBuffer, mvpSetLayout, &scene);
        postprocessing->handleResize(lighting->finalLight, gBuffer);
    } else {
        VK_CHECK_RESULT(result)
    }
}

void JungleApp::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    if (fullscreen) {
        auto monitor = glfwGetPrimaryMonitor();
        auto mode = glfwGetVideoMode(monitor);
        window = glfwCreateWindow(mode->width, mode->height, "Bioluminescent Jungle", monitor, nullptr);
    } else {
        window = glfwCreateWindow(WIDTH, HEIGHT, "Bioluminescent Jungle", nullptr, nullptr);
    }
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, handleGLFWResize);
    glfwSetCursorPosCallback(window, handleGLFWMouse);
    glfwSetScrollCallback(window, handleGLFWScrolling);
}

// Adapted from: https://github.com/ocornut/imgui/blob/master/examples/example_glfw_vulkan/main.cpp
// Beginning of section Copyright (c) 2014-2024 Omar Cornut.
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
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
            };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = (uint32_t) IM_ARRAYSIZE(poolSizes);
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
    ImGui_ImplVulkan_Init(&init_info, postprocessing->getFinalRenderPass());

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
// End of section Copyright (c) 2014-2024 Omar Cornut.

void JungleApp::createSurface() {
    VK_CHECK_RESULT(glfwCreateWindowSurface(device.instance, window, nullptr, &surface))
}

void JungleApp::recompileShaders() {
    vkDeviceWaitIdle(device);

    scene.createPipelines(sceneRPass, mvpSetLayout, true);
    lighting->createPipeline(true, mvpSetLayout, &scene);
    lighting->getDenoiser()->createPipeline(true);
    postprocessing->createPipeline(true);
}

void JungleApp::createScenePass() {
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> colorAttachments;
    VkAttachmentReference depthAttachmentRef;

    for (int i = 0; i < GBufferTarget::NumAttachments; i++) {
        auto fmt = getGBufferAttachmentFormat(swapchain.get(), (GBufferTarget) i);
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
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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
    renderPassInfo.framebuffer = gBuffer.framebuffers[sceneRPass][currentFrame];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain->renderSize();

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
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr; // Optional
    layoutBindings.push_back(uboLayoutBinding);
    uboLayoutBinding.binding = 1;
    layoutBindings.push_back(uboLayoutBinding);

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = layoutBindings.size();
    layoutInfo.pBindings = layoutBindings.data();
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &mvpSetLayout))
}

void JungleApp::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    mvpUBO.allocate(&device, bufferSize, MAX_FRAMES_IN_FLIGHT);
    lastmvpUBO.allocate(&device, bufferSize, MAX_FRAMES_IN_FLIGHT);
    lighting->setupBuffers();
    postprocessing->setupBuffers();
}

void JungleApp::updateUniformBuffers(uint32_t currentImage) {
    static auto startTime = std::chrono::high_resolution_clock::now();
    static float lastTime = 0;

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
    if (!doMotion) {
        startTime += duration_cast<std::chrono::high_resolution_clock::duration>(
                std::chrono::duration<float, std::chrono::seconds::period>(time - lastTime)
        );
        time = lastTime;
    }
    float rotation = spinScene ? time * glm::radians(90.0f) : glm::radians(fixedRotation);

    UniformBufferObject ubo{};
    ubo.modl = glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(cameraPosition, cameraLookAt, cameraUpVector);
    ubo.proj = glm::perspective(glm::radians(cameraFOVY),
                                (float) swapchain->renderSize().width / (float) swapchain->renderSize().height,
                                nearPlane, farPlane);
    ubo.proj[1][1] *= -1;  // because GLM generates OpenGL projections
    if (doJitter) {
        ubo.jitt = halton23norm(jitterSequence);
        ubo.jitt *= glm::vec2(1.f / swapchain->finalBufferSize.width, 1.f / swapchain->finalBufferSize.height);
        jitterSequence++;
    } else {
        ubo.jitt = glm::vec2(0, 0);
    }
    ubo.time = time;

    mvpUBO.copyTo(lastmvpUBO, (currentImage + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT, currentImage,
                  sizeof(ubo));
    mvpUBO.update(&ubo, sizeof(ubo), currentImage);
    scene.updateBuffers(time, cameraPosition, time - lastTime);
    lighting->updateBuffers(ubo.proj * ubo.view, cameraPosition, cameraUpVector);
    lighting->getDenoiser()->updateCamera(ubo.proj);

    // TODO: is there a better way to integrate this somehow? Too lazy to skip the tonemapping render pass completely.
    if (lighting->useDebugPipeline()) {
        postprocessing->disable();
    } else {
        postprocessing->enable();
    }

    postprocessing->getFogPointer()->updateCamera(ubo.view, ubo.proj, nearPlane, farPlane);
    postprocessing->updateBuffers();
    lastTime = time;
}

void JungleApp::createDescriptorPool() {

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

    std::vector<RequiredDescriptors> requirements = {
            scene.getNumDescriptors(), lighting->getNumDescriptors(), postprocessing->getNumDescriptors()
    };

    // Descriptors required in JungleApp itself
    requirements.push_back({
                                   .requireUniformBuffers = MAX_FRAMES_IN_FLIGHT * 2,
                                   .requireSamplers = 0,
                           });

    for (auto &req: requirements) {
        poolSizes[0].descriptorCount += req.requireUniformBuffers;
        poolSizes[1].descriptorCount += req.requireSamplers;
        poolSizes[2].descriptorCount += req.requireSSBOs;
    }

    poolSizes[0].descriptorCount += 100;
    poolSizes[1].descriptorCount += 100;
    poolSizes[2].descriptorCount += 100;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = poolSizes[0].descriptorCount + poolSizes[1].descriptorCount + poolSizes[2].descriptorCount;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool))
}

void JungleApp::createDescriptorSets() {
    sceneDescriptorSets = VulkanHelper::createDescriptorSetsFromLayout(device, descriptorPool,
                                                                       mvpSetLayout, MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mvpUBO.buffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);
        bufferInfos.push_back(bufferInfo);
        bufferInfo.buffer = lastmvpUBO.buffers[i];
        bufferInfos.push_back(bufferInfo);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = sceneDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = bufferInfos.size();
        descriptorWrite.pBufferInfo = bufferInfos.data();
        descriptorWrite.pImageInfo = nullptr; // Optional
        descriptorWrite.pTexelBufferView = nullptr; // Optional
        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }

    scene.setupDescriptorSets(descriptorPool);
    lighting->createDescriptorSets(descriptorPool, gBuffer, &scene);
    postprocessing->createDescriptorSets(descriptorPool, lighting->finalLight, gBuffer);
}

void JungleApp::cleanup() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    swapchain.reset();

    mvpUBO.destroy(&device);
    lastmvpUBO.destroy(&device);
    lighting.reset();
    postprocessing.reset();
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

void JungleApp::setupScene(const std::string &sceneName) {
    scene = Scene(&device, swapchain.get(), sceneName);
    scene.setupBuffers();
    scene.setupTextures();
    scene.computeDefaultCameraPos(cameraFinalLookAt, cameraFinalPosition, cameraUpVector, cameraFOVY, nearPlane, farPlane);
}

void JungleApp::handleMotion() {
    glm::vec3 viewDir = cameraFinalLookAt - cameraFinalPosition;
    glm::vec3 fwd = glm::normalize(glm::vec3(viewDir.x, viewDir.y, 0.0));
    glm::vec3 side = glm::cross(fwd, glm::vec3(0.0, 0.0, 1.0));

    // Camera movement
    glm::vec3 movement(0.0);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        movement += fwd;

    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        movement -= fwd;

    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        movement -= side;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        movement += side;

    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        movement.z += std::copysign(1, cameraUpVector.z);
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        movement.z -= 1.0;

    if (glm::length(movement) <= 1e-6) {
        lastMoveTime = -1;
        return;
    }

    float curTime = glfwGetTime();
    if (lastMoveTime < 0) {
        lastMoveTime = curTime;
        return;
    }

    movement *= curTime - lastMoveTime;
    movement *= cameraMovementSpeed;
    cameraFinalPosition += movement;
    cameraFinalLookAt += movement;
    cameraUpVector = glm::vec3(0, 0, 1);
    lastMoveTime = curTime;
}

void JungleApp::handleGLFWScrolling(GLFWwindow* window, double xoffset, double yoffset) {
    if (ImGui::GetIO().WantCaptureMouse) {
        // ImGui is using the mouse at the moment
        return;
    }

    auto app = reinterpret_cast<JungleApp*>(glfwGetWindowUserPointer(window));

    app->cameraMovementSpeed *= pow(2, yoffset/4);

    if (app->cameraMovementSpeed > 102.4) {
        app->cameraMovementSpeed = 102.4;
    }

    if (app->cameraMovementSpeed < 0.05) {
        app->cameraMovementSpeed = 0.05;
    }
}


void JungleApp::handleGLFWMouse(GLFWwindow *window, double x, double y) {
    if (ImGui::GetIO().WantCaptureMouse) {
        // ImGui is using the mouse at the moment
        return;
    }

    auto app = reinterpret_cast<JungleApp*>(glfwGetWindowUserPointer(window));
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) {
        // Button not pressed => don't do anything
        app->lastMouseX.reset();
        app->lastMouseY.reset();
        return;
    }

    if (app->lastMouseX.has_value() && app->lastMouseY.has_value()) {
        // We are dragging the mouse with button pressed
        float dx = -(app->lastMouseX.value() - x) * 180 / app->swapchain->finalBufferSize.width;
        float dy = (y - app->lastMouseY.value()) * 180 / app->swapchain->finalBufferSize.height;
        if (app->invertMouse) {
            dx *= -1;
            dy *= -1;
        }

        // Button is pressed, we have previous values => compute change in the target
        glm::vec3 viewDir = glm::normalize(app->cameraFinalLookAt - app->cameraFinalPosition);
        dx *= std::copysign(1, app->cameraUpVector.z);
        dy *= std::copysign(1, app->cameraUpVector.z);

        float yaw = glm::degrees(atan2(viewDir.y, viewDir.x));
        float pitch = glm::degrees(asin(viewDir.z));

        yaw += dx;
        pitch = std::clamp(pitch + dy, -89.9f, 89.9f);

        viewDir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        viewDir.z = sin(glm::radians(pitch));
        viewDir.y = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        app->cameraFinalLookAt = app->cameraFinalPosition + viewDir;
    }

    // store x,y values for the next event
    app->lastMouseX = x;
    app->lastMouseY = y;
    app->cameraUpVector = glm::vec3(0, 0, 1);
}

void JungleApp::handleGLFWResize(GLFWwindow* window, int width, int height)
{
    auto app = reinterpret_cast<JungleApp*>(glfwGetWindowUserPointer(window));
    app->framebufferResized = true;
}

constexpr float easeAnimation(float alpha) {
    return 1.0 - std::pow(1.0 - alpha, 5);
}

void JungleApp::cameraMotion() {
    const float EPS = 0.001f;

    glm::vec3 delta = this->cameraFinalPosition - this->cameraPosition;
    if (glm::length(delta) < EPS || lastMoveTime >= 0) {
        this->cameraPosition = this->cameraFinalPosition;
        this->cameraLookAt = this->cameraFinalLookAt;
        return;
    }

    const auto& restartAnimation = [&] {
        lastCameraChange = glfwGetTime();
        cameraAnimStartPos = cameraPosition;
        cameraAnimEndPos = cameraFinalPosition;
    };

    if (!lastCameraChange.has_value()) {
        restartAnimation();
        return;
    }

    // Compute time elapsed
    const double now = glfwGetTime();
    const double elapsed = std::min(1.0, (now - *lastCameraChange) * 4.0);
    const bool animationChanged = glm::length(cameraAnimEndPos - cameraFinalPosition) >= EPS;

    // Ease the animation
    const float alpha = easeAnimation(elapsed);
    this->cameraPosition = (1.0f - alpha) * this->cameraAnimStartPos + alpha * this->cameraAnimEndPos;
    this->cameraLookAt = (this->cameraFinalLookAt - this->cameraFinalPosition) + this->cameraPosition;

    // If the camera position changes while we are animating, adjust the start/end points again
    if (animationChanged) {
        restartAnimation();
    }
}

float JungleApp::halton(uint32_t b, uint32_t n) {
    // from wikipedia
    float f = 1;
    float r = 0;
    while (n > 0) {
        f = f / b;
        r = r + f * (n % b);
        n /= b;
    }
    return r;
}

glm::vec2 JungleApp::halton23norm(uint32_t n) {
    return glm::vec2(halton(2, n), halton(3, n)) * 2.f - 1.f;
}

void JungleApp::handleHeight() {
    if (cameraFixedHeight) {
        glm::vec3 above{cameraFinalPosition};
        above.z = 200;
        glm::vec3 dir{0, 0, -1};
        auto t = this->groundBVH->intersectRay(above, dir);
        if (t.has_value()) {
            float cameraZDelta = above.z - t.value() * glm::length(dir) + cameraHeightAboveGround - cameraFinalPosition.z;
            cameraFinalPosition.z += cameraZDelta;
            cameraFinalLookAt.z += cameraZDelta;
        }
    }
}
