//
// Created by lars on 30.10.23.
//

#ifndef JUNGLE_VULKANHELPER_H
#define JUNGLE_VULKANHELPER_H


#include "tiny_gltf.h"
#include <vulkan/vulkan.h>
#include "Scene.h"
#include <glm/glm.hpp>
#include <shaderc/shaderc.hpp>
#include <fstream>
#include <optional>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <stdexcept>
#include <iostream>
#include <GLFW/glfw3.h>

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

static std::tuple<std::vector<char>, std::string> getShaderCode(const std::string &filename, shaderc_shader_kind kind, bool recompile) {
    std::string message;
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
            message = "Shader compilation failed:\n" + result.GetErrorMessage();
        } else {
            std::vector<char> spirv = {reinterpret_cast<const char *>(result.cbegin()),
                                       reinterpret_cast<const char *>(result.cend())};

            writeFile(spvFilename, spirv);
        }
    }
    return {readFile(spvFilename), message};
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

class VulkanHelper {

    static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

    static void copyBuffer(VkDevice device, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
                           VkCommandPool commandPool, VkQueue queue);

public:
    static void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory);

    static void uploadBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize bufferSize, VkBuffer buffer,
                      const void *data, VkCommandPool commandPool, VkQueue queue);

    static VkFormat gltfTypeToVkFormat(int type, int componentType, bool normalized);

    static uint32_t strideFromGltfType(int type, int componentType, size_t stride);

    static VkIndexType gltfTypeToVkIndexType(int componentType);

    static glm::mat4
    transformFromMatrixOrComponents(std::vector<double> matrix, std::vector<double> scale, std::vector<double> rotation,
                                    std::vector<double> translation);
};


#endif //JUNGLE_VULKANHELPER_H
