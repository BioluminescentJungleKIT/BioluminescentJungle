//
// Created by lars on 31.07.23.
//

#ifndef VULKANBASICS_PLANETAPP_H
#define VULKANBASICS_PLANETAPP_H

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <array>
#include <shaderc/shaderc.hpp>
#include <glm/glm.hpp>
#include "Scene.h"
#include "PhysicalDevice.h"
#include "Swapchain.h"

const uint32_t WIDTH = 1800;
const uint32_t HEIGHT = 1200;

const int MAX_FRAMES_IN_FLIGHT = 2;

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

class JungleApp {
public:
    void run(const std::string& sceneName) {
        initWindow();
        initVulkan(sceneName);
        initImGui();
        mainLoop();
        cleanup();
    }

private:
    void initWindow();

    void initVulkan(const std::string& sceneName);

    void initImGui();

    void mainLoop();

    void cleanup();

    VulkanDevice device;
    std::unique_ptr<Swapchain> swapchain;

    GLFWwindow *window;
    VkSurfaceKHR surface;

    void createSurface();

    void createGraphicsPipeline(bool recompileShaders);

    VkShaderModule createShaderModule(std::vector<char> code);

    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    void createRenderPass();

    VkCommandPool commandPool;

    void createCommandPool();

    std::vector<VkCommandBuffer> commandBuffers;

    void createCommandBuffer();

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    bool framebufferResized = false;

    uint32_t currentFrame = 0;

    void drawFrame();

    void createSyncObjects();

    static void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
        auto app = reinterpret_cast<JungleApp *>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;
    }

    void createDescriptorSetLayout();

    VkDescriptorSetLayout descriptorSetLayout;

    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void *> uniformBuffersMapped;

    void createUniformBuffers();

    void updateUniformBuffer(uint32_t currentImage);

    void createDescriptorPool();

    VkDescriptorPool descriptorPool;
    VkDescriptorPool imguiDescriptorPool;

    void createDescriptorSets();

    std::vector<VkDescriptorSet> descriptorSets;

    void setupScene(const std::string& sceneName);

    Scene scene;

    bool showMetricsWindow;
    bool forceRecreateSwapchain;
    bool forceReloadShaders;
    bool showDemoWindow;
    std::string lastVertMessage;
    std::string lastFragMessage;
    float nearPlane = .1f;
    float farPlane = 1000.f;
    float cameraFOVY = 45;
    glm::vec3 cameraLookAt = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 cameraPosition = glm::vec3(5.0f, 5.0f, 5.0f);
    glm::vec3 cameraUpVector = glm::vec3(0.0f, 0.0f, 1.0f);
    bool spinScene = true;
    float fixedRotation = 0.0;

    void cleanupGraphicsPipeline();

    void recreateGraphicsPipeline();
};


#endif //VULKANBASICS_PLANETAPP_H
