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
};


#endif //JUNGLE_VULKANHELPER_H
