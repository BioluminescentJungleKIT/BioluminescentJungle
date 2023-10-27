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
#include <shaderc/shaderc.hpp>
#include <glm/glm.hpp>

static std::string errorString(VkResult errorCode) {
    switch (errorCode) {
#define STR(r) case VK_ ##r: return #r
        STR(NOT_READY);                                           \
        STR(TIMEOUT);                                             \
        STR(EVENT_SET);                                           \
        STR(EVENT_RESET);                                         \
        STR(INCOMPLETE);                                          \
        STR(ERROR_OUT_OF_HOST_MEMORY);                            \
        STR(ERROR_OUT_OF_DEVICE_MEMORY);                          \
        STR(ERROR_INITIALIZATION_FAILED);                         \
        STR(ERROR_DEVICE_LOST);                                   \
        STR(ERROR_MEMORY_MAP_FAILED);                             \
        STR(ERROR_LAYER_NOT_PRESENT);                             \
        STR(ERROR_EXTENSION_NOT_PRESENT);                         \
        STR(ERROR_FEATURE_NOT_PRESENT);                           \
        STR(ERROR_INCOMPATIBLE_DRIVER);                           \
        STR(ERROR_TOO_MANY_OBJECTS);                              \
        STR(ERROR_FORMAT_NOT_SUPPORTED);                          \
        STR(ERROR_SURFACE_LOST_KHR);                              \
        STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);                      \
        STR(SUBOPTIMAL_KHR);                                      \
        STR(ERROR_OUT_OF_DATE_KHR);                               \
        STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);                      \
        STR(ERROR_VALIDATION_FAILED_EXT);                         \
        STR(ERROR_INVALID_SHADER_NV);                             \
        STR(ERROR_FRAGMENTED_POOL);                               \
        STR(ERROR_UNKNOWN);                                       \
        STR(ERROR_OUT_OF_POOL_MEMORY);                            \
        STR(ERROR_INVALID_EXTERNAL_HANDLE);                       \
        STR(ERROR_FRAGMENTATION);                                 \
        STR(ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);                \
        STR(PIPELINE_COMPILE_REQUIRED);                           \
        STR(ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);  \
        STR(ERROR_NOT_PERMITTED_KHR);                             \
        STR(ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);           \
        STR(THREAD_IDLE_KHR);                                     \
        STR(THREAD_DONE_KHR);                                     \
        STR(OPERATION_DEFERRED_KHR);                              \
        STR(OPERATION_NOT_DEFERRED_KHR);
#undef STR
        default:
            return "UNKNOWN_ERROR";
    }
}


#define VK_CHECK_RESULT(f)                                                                                \
{                                                                                                         \
    VkResult res = (f);                                                                                   \
    if (res != VK_SUCCESS)                                                                                \
    {                                                                                                     \
        std::cout << "Fatal : VkResult is \"" << errorString(res) << "\" in "                            \
        << __FILE__ << " at line " << __LINE__ << "\n";                                                   \
        assert(res == VK_SUCCESS);                                                                        \
    }                                                                                                     \
}

const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

const int MAX_FRAMES_IN_FLIGHT = 2;

const std::vector<const char *> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);
        return attributeDescriptions;
    }
};

const float t = (1.0f + sqrtf(5.0f)) / 2.0f;

const std::vector<Vertex> vertices = {
        {{-1.0f, t,     0.0f},  {1.0f, 0.0f, 0.0f}},
        {{1.0f,  t,     0.0f},  {0.0f, 1.0f, 0.0f}},
        {{-1.0f, -t,    0.0f},  {0.0f, 0.0f, 1.0f}},
        {{1.0f,  -t,    0.0f},  {1.0f, 1.0f, 0.0f}},
        {{0.0f,  -1.0f, t},     {0.0f, 1.0f, 1.0f}},
        {{0.0f,  1.0f,  t},     {1.0f, 0.0f, 1.0f}},
        {{0.0f,  -1.0f, -t},    {1.0f, 1.0f, 1.0f}},
        {{0.0f,  1.0f,  -t},    {1.0f, 0.0f, 0.0f}},
        {{t,     0.0f,  -1.0f}, {0.0f, 1.0f, 0.0f}},
        {{t,     0.0f,  1.0f},  {0.0f, 0.0f, 1.0f}},
        {{-t,    0.0f,  -1.0f}, {1.0f, 1.0f, 1.0f}},
        {{-t,    0.0f,  1.0f},  {0.0f, 0.0f, 0.0f}}
};

const std::vector<uint16_t> vertex_indices = {
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
        1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
        3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
        4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1
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

static std::vector<char> readFile(const std::string &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    auto fileSize = file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

static void writeFile(const std::string &filename, const std::vector<char> &buffer) {
    std::ofstream file(filename, std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    file.write(buffer.data(), (std::streamsize) buffer.size());
    file.close();
}

static bool fileExists(const std::string &filename) {
    std::ifstream file(filename);
    return file.good();
}

static auto getShaderCode(const std::string &filename, shaderc_shader_kind kind, bool recompile) {
    auto sourceName = filename.substr(filename.find_last_of("/\\") + 1, filename.find_last_of('.'));
    auto spvFilename = filename + ".spv";
    if (recompile || !fileExists(spvFilename)) {
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        auto file_content = readFile(filename);
        file_content.push_back('\0');
        std::string source = file_content.data();
        shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
                source, kind, sourceName.c_str(), options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
            throw std::runtime_error("Shader compilation failed:\n" + result.GetErrorMessage());
        }

        std::vector<char> spirv = {reinterpret_cast<const char *>(result.cbegin()),
                                   reinterpret_cast<const char *>(result.cend())};

        writeFile(spvFilename, spirv);
    }
    return readFile(spvFilename);
}


class JungleApp {
public:
    void run() {
        initWindow();
        initVulkan();
        initImGui();
        mainLoop();
        cleanup();
    }

private:
    void initWindow();

    void initVulkan();

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

    void createGraphicsPipeline();

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

    void createVertexBuffer();

    VkBuffer vertexBuffer;

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkDeviceMemory vertexBufferMemory;

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer,
                      VkDeviceMemory &bufferMemory);

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;

    void createIndexBuffer();

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
};


#endif //VULKANBASICS_PLANETAPP_H
