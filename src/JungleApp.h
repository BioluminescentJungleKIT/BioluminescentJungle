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
#include <vector>
#include <cstring>
#include <optional>
#include <fstream>
#include <array>
#include <shaderc/shaderc.hpp>
#include <glm/glm.hpp>
#include "Scene.h"

const uint32_t WIDTH = 1800;
const uint32_t HEIGHT = 1200;

const int MAX_FRAMES_IN_FLIGHT = 2;

const std::vector<const char *> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif


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

    GLFWwindow *window;

    void createInstance();

    VkInstance instance;

    static bool checkValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char *layerName: validationLayers) {
            bool layerFound = false;

            for (const auto &layerProperties: availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound) {
                return false;
            }
        }

        return true;
    }

    std::vector<const char *> getRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
            void *pUserData) {

        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
        if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            // Message is important enough to show
        }

        return VK_FALSE;
    }

    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
                                          const VkAllocationCallbacks *pAllocator,
                                          VkDebugUtilsMessengerEXT *pDebugMessenger) {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance,
                                                                               "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger,
                                       const VkAllocationCallbacks *pAllocator) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance,
                                                                                "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, pAllocator);
        }
    }

    VkDebugUtilsMessengerEXT debugMessenger;

    void setupDebugMessenger();

    void pickPhysicalDevice();

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;

    bool isDeviceSuitable(VkPhysicalDevice const &device);

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() {
            return graphicsFamily.has_value() && presentFamily.has_value();
        };
    };

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    VkDevice device;

    void createLogicalDevice();

    VkQueue graphicsQueue;

    VkSurfaceKHR surface;

    void createSurface();

    VkQueue presentQueue;

    bool checkDeviceExtensionSupport(VkPhysicalDevice const &device);

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);

    VkSwapchainKHR swapChain;

    void createSwapChain();

    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;

    std::vector<VkImageView> swapChainImageViews;

    void createImageViews();

    void createGraphicsPipeline(bool recompileShaders);

    VkShaderModule createShaderModule(std::vector<char> code);

    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    void createRenderPass();

    std::vector<VkFramebuffer> swapChainFramebuffers;

    void createFramebuffers();

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

    void cleanupSwapChain();

    void recreateSwapChain();

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

    void createDescriptorSets();

    std::vector<VkDescriptorSet> descriptorSets;

    void setupScene(const std::string& sceneName);

    Scene scene;

    bool enableVSync = false;
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
