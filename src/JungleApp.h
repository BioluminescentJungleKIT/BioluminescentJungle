// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#ifndef VULKANBASICS_PLANETAPP_H
#define VULKANBASICS_PLANETAPP_H

#include <memory>
#include "BVH.hpp"
#include <shaderc/shaderc.hpp>
#include "Lighting.h"
#include "Scene.h"
#include "PhysicalDevice.h"
#include "Swapchain.h"
#include "MusicPlayer.h"
#include "PostProcessing.h"

const uint32_t WIDTH = 1800;
const uint32_t HEIGHT = 1200;

struct alignas(16) UniformBufferObject {
    glm::mat4 modl;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec2 jitt;
    float time;
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

    bool fullscreen = false;

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

    std::optional<float> lastMouseX, lastMouseY;
    static void handleGLFWResize(GLFWwindow* window, int width, int height);
    void handleMotion();
    static void handleGLFWMouse(GLFWwindow *window, double x, double y);
    static void handleGLFWScrolling(GLFWwindow *window, double xoffset, double yoffset);

    void createMVPSetLayout();

    VkDescriptorSetLayout mvpSetLayout;
    UniformBuffer lastmvpUBO;
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
    bool switchFullscreen;
    bool forceReloadShaders;
    bool showDemoWindow;
    bool invertMouse = true;
    float nearPlane = .1f;
    float farPlane = 1000.f;
    float cameraFOVY = 45;
    glm::vec3 cameraLookAt = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 cameraPosition = glm::vec3(5.0f, 5.0f, 5.0f);
    glm::vec3 cameraUpVector = glm::vec3(0.0f, 0.0f, 1.0f);
    bool cameraFixedHeight{false};
    float cameraHeightAboveGround{0.8};
    float cameraMovementSpeed = 2.0;

    double lastMoveTime = -1.0;
    // The *Final* members indicate what is the current target for the camera (while the animation is running)
    glm::vec3 cameraFinalPosition = glm::vec3(5.0f, 5.0f, 5.0f);

    glm::vec3 cameraFinalLookAt = glm::vec3(5.0f, 5.0f, 5.0f);
    // Parameters while computing the animation for the camera
    glm::vec3 cameraAnimStartPos = cameraFinalPosition;
    bool illuminationViaButterflies = false;

    glm::vec3 cameraAnimEndPos = cameraFinalPosition;
    std::optional<double> lastCameraChange;

    void cameraMotion();
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
    bool doMotion = true;

    std::unique_ptr<BVH> groundBVH;

    void handleHeight();
};


#endif //VULKANBASICS_PLANETAPP_H
