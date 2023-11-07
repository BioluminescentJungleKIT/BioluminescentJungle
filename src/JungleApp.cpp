#include "Pipeline.h"
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
    createRenderPass();
    setupScene(sceneName);
    createDescriptorSetLayout();
    createGraphicsPipeline(recompileShaders);
    swapchain->createFramebuffersForRender(renderPass);
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffer();
    createSyncObjects();
}

void JungleApp::mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        drawFrame();
    }

    vkDeviceWaitIdle(device.device);
}

void JungleApp::drawFrame() {
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapchain->swapChain, UINT64_MAX,
        imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain->recreateSwapChain(renderPass);
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        VK_CHECK_RESULT(result)
    }

    if (forceReloadShaders) {
        recreateGraphicsPipeline();
    }

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

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
            ImGui::SliderFloat("Exposure", &exposure, -10, 10);
            ImGui::SliderFloat("Gamma", &gamma, 0, 4);
            ImGui::Combo("Tonemapping", &tonemappingMode, "None\0Hable\0AgX\0\0");
        }
    }
    ImGui::End();

    if (showMetricsWindow) {
        ImGui::ShowMetricsWindow(&showMetricsWindow);
    }
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    for (auto& [stage, msg] : pipeline->errorsFromShaderCompilation) {
        if (ImGui::Begin(stage.c_str())) {
            ImGui::Text("%s", msg.c_str());
        }
        ImGui::End();
    }

    updateUniformBuffers(currentFrame);

    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[currentFrame];
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    VK_CHECK_RESULT(vkQueueSubmit(device.graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]))
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {swapchain->swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr; // Optional

    result = vkQueuePresentKHR(device.presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized || forceRecreateSwapchain) {
        framebufferResized = false;
        forceRecreateSwapchain = false;
        swapchain->recreateSwapChain(renderPass);
    } else {
        VK_CHECK_RESULT(result)
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
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
    ImGui_ImplVulkan_Init(&init_info, renderPass);

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

void JungleApp::createGraphicsPipeline(bool recompileShaders) {
    PipelineParameters params;

    std::string shaderNames = scene.queryShaderName();
    params.shadersList = {
        {VK_SHADER_STAGE_VERTEX_BIT, shaderNames + ".vert"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, shaderNames + ".frag"},
    };

    params.recompileShaders = recompileShaders;

    auto [attributeDescriptions, bindingDescriptions] = scene.getAttributeAndBindingDescriptions();
    params.vertexAttributeDescription = attributeDescriptions;
    params.vertexInputDescription = bindingDescriptions;
    params.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    params.extent = swapchain->swapChainExtent;

    // One color attachment, no blending enabled for it
    params.blending = {{}};
    params.useDepthTest = true;

    params.descriptorSetLayouts = scene.getDescriptorSetLayouts(device);
    params.descriptorSetLayouts.insert(params.descriptorSetLayouts.begin(), descriptorSetLayout);
    this->pipeline = std::make_unique<GraphicsPipeline>(&device, renderPass, 0, params);

}

void JungleApp::recreateGraphicsPipeline() {
    vkDeviceWaitIdle(device);
    pipeline.reset();
    createGraphicsPipeline(true);
}
void JungleApp::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchain->swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = swapchain->chooseDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass))
}

void JungleApp::createCommandBuffer() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = device.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();

    VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()))
}

void JungleApp::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0; // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo))

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchain->swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain->swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain->swapChainExtent.width);
    viewport.height = static_cast<float>(swapchain->swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain->swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    //vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
    //                        &descriptorSets[currentFrame], 0, nullptr);

    scene.render(commandBuffer, pipeline->layout, descriptorSets[currentFrame]);

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer))
}

void JungleApp::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]))
        VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]))
        VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]))
    }
}

void JungleApp::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

    VkDescriptorSetLayoutBinding tonemappingLayoutBinding{};
    tonemappingLayoutBinding.binding = 1;
    tonemappingLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    tonemappingLayoutBinding.descriptorCount = 1;
    tonemappingLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    tonemappingLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings{uboLayoutBinding, tonemappingLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout))
}

void JungleApp::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VulkanHelper::createBuffer(device, device.physicalDevice, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   uniformBuffers[i],
                                   uniformBuffersMemory[i]);

        vkMapMemory(device, uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
    }

    VkDeviceSize tonemappingBufferSize = sizeof(TonemappingUBO);

    VulkanHelper::createBuffer(device, device.physicalDevice, tonemappingBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               tonemappingUniformBuffer,
                               tonemappingUniformBufferMemory);

    vkMapMemory(device, tonemappingUniformBufferMemory, 0, tonemappingBufferSize, 0, &tonemappingBufferMapped);
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

    memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));

    TonemappingUBO tonemapping{};
    tonemapping.exposure = exposure;
    tonemapping.gamma = gamma;
    tonemapping.mode = tonemappingMode;
    memcpy(tonemappingBufferMapped, &tonemapping, sizeof(tonemapping));
}

void JungleApp::createDescriptorPool() {

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    poolSizes[0].descriptorCount += static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT); // For tonemapping UBOs
    poolSizes[0].descriptorCount += scene.getNumDescriptorSets();

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = scene.getNumTextures();

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = &poolSizes[0];
    poolInfo.maxSets = poolSizes[0].descriptorCount + poolSizes[1].descriptorCount;
    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool))
}

void JungleApp::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()))

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        descriptorWrite.pImageInfo = nullptr; // Optional
        descriptorWrite.pTexelBufferView = nullptr; // Optional

        VkDescriptorBufferInfo tonemappingBufferInfo{};
        tonemappingBufferInfo.buffer = tonemappingUniformBuffer;
        tonemappingBufferInfo.offset = 0;
        tonemappingBufferInfo.range = sizeof(TonemappingUBO);

        VkWriteDescriptorSet tonemappingDescriptorWrite{};
        tonemappingDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        tonemappingDescriptorWrite.dstSet = descriptorSets[i];
        tonemappingDescriptorWrite.dstBinding = 1;
        tonemappingDescriptorWrite.dstArrayElement = 0;
        tonemappingDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        tonemappingDescriptorWrite.descriptorCount = 1;
        tonemappingDescriptorWrite.pBufferInfo = &tonemappingBufferInfo;
        tonemappingDescriptorWrite.pImageInfo = nullptr; // Optional
        tonemappingDescriptorWrite.pTexelBufferView = nullptr; // Optional

        std::vector<VkWriteDescriptorSet> descriptorWrites{descriptorWrite, tonemappingDescriptorWrite};

        vkUpdateDescriptorSets(device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }

    scene.setupDescriptorSets(device, descriptorPool, descriptorSetLayout);
}

void JungleApp::cleanup() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    swapchain.reset();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(device, uniformBuffers[i], nullptr);
        vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
    }
    vkDestroyBuffer(device, tonemappingUniformBuffer, nullptr);
    vkFreeMemory(device, tonemappingUniformBufferMemory, nullptr);

    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr);

    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    scene.destroyDescriptorSetLayout(device);
    scene.destroyTextures(&device);
    scene.destroyBuffers(device);
    pipeline.reset();

    vkDestroyRenderPass(device, renderPass, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(device, inFlightFences[i], nullptr);
    }

    vkDestroySurfaceKHR(device.instance, surface, nullptr);
    device.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void JungleApp::setupScene(const std::string& sceneName) {
    scene = Scene(sceneName);
    scene.setupBuffers(&device);
    scene.setupTextures(&device);
    scene.computeCameraPos(cameraLookAt, cameraPosition, cameraFOVY);
}
