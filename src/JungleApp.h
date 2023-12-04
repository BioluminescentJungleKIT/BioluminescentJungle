//
// Created by lars on 31.07.23.
//

#ifndef VULKANBASICS_PLANETAPP_H
#define VULKANBASICS_PLANETAPP_H

#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <array>
#include <shaderc/shaderc.hpp>
#include "Lighting.h"
#include "Scene.h"
#include "PhysicalDevice.h"
#include "Swapchain.h"
#include "MusicPlayer.h"
#include "Pipeline.h"
#include "Tonemap.h"
#include "PostProcessing.h"

const uint32_t WIDTH = 1800;
const uint32_t HEIGHT = 1200;

struct UniformBufferObject {
    glm::mat4 modl;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec2 jitt;
};

class JungleApp {
public:
    void run(const std::string &sceneName, bool recompileShaders) {
        initWindow();
        initVulkan(sceneName, recompileShaders);
        initImGui();
        mplayer.init();
        mainLoop();
        cleanup();
        mplayer.terminate();
    }

private:
    void initWindow();

    void initVulkan(const std::string &sceneName, bool recompileShaders);

    void initImGui();

    void mainLoop();

    void cleanup();

    VulkanDevice device;
    std::unique_ptr<Swapchain> swapchain;

    GLFWwindow *window;
    VkSurfaceKHR surface;

    void createSurface();

    void setupRenderStageScene(const std::string &sceneName, bool recompileShaders);

    std::unique_ptr<PostProcessing> postprocessing;
    std::unique_ptr<DeferredLighting> lighting;

    VkRenderPass sceneRPass;
    RenderTarget gBuffer;

    void createScenePass();

    std::vector<VkCommandBuffer> commandBuffers;

    void createCommandBuffers();

    void startRenderPass(VkCommandBuffer commandBuffer, uint32_t currentFrame, VkRenderPass renderPass);

    bool framebufferResized = false;

    void drawFrame();

    void drawImGUI();

    static void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
        auto app = reinterpret_cast<JungleApp *>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    void createMVPSetLayout();

    VkDescriptorSetLayout mvpSetLayout;
    UniformBuffer mvpUBO;

    void createUniformBuffers();

    void updateUniformBuffers(uint32_t currentImage);

    void createDescriptorPool();

    VkDescriptorPool descriptorPool;
    VkDescriptorPool imguiDescriptorPool;

    void createDescriptorSets();

    std::vector<VkDescriptorSet> sceneDescriptorSets;

    void setupScene(const std::string &sceneName);

    Scene scene;

    bool showMetricsWindow;
    bool forceRecreateSwapchain;
    bool forceReloadShaders;
    bool showDemoWindow;
    float nearPlane = .1f;
    float farPlane = 1000.f;
    float cameraFOVY = 45;
    glm::vec3 cameraLookAt = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 cameraPosition = glm::vec3(5.0f, 5.0f, 5.0f);
    glm::vec3 cameraUpVector = glm::vec3(0.0f, 0.0f, 1.0f);
    bool spinScene = false;
    float fixedRotation = 0.0;

    void recompileShaders();

    void setupGBuffer();

    float halton(uint32_t b, uint32_t n);
    glm::vec2 halton23norm(uint32_t n);

    MusicPlayer mplayer{"scenes/loop.wav"};
    bool playMusic;
    uint32_t jitterSequence = 0;
    bool doJitter = true;
};


#endif //VULKANBASICS_PLANETAPP_H
