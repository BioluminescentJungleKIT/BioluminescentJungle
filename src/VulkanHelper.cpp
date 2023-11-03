//
// Created by lars on 30.10.23.
//

#include <glm/gtc/quaternion.hpp>
#include "Scene.h"
#include <iostream>
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"
#include "imgui.h"
#include "JungleApp.h"
#include <chrono>
#include <algorithm>
#include <limits>
#include <set>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/glm.hpp>
#include "VulkanHelper.h"

void
VulkanHelper::createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                           VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK_RESULT(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer))

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    VK_CHECK_RESULT(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory))

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

uint32_t
VulkanHelper::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

void VulkanHelper::copyBuffer(VkDevice device, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
                              VkCommandPool commandPool, VkQueue queue) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0; // Optional
    copyRegion.dstOffset = 0; // Optional
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void
VulkanHelper::uploadBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize bufferSize, VkBuffer buffer,
                           const void *data, VkCommandPool commandPool, VkQueue queue) {
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(device, physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                 stagingBufferMemory);

    void *bufferData;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &bufferData);
    memcpy(bufferData, data, (size_t) bufferSize);
    vkUnmapMemory(device, stagingBufferMemory);

    copyBuffer(device, stagingBuffer, buffer, bufferSize, commandPool, queue);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

VkFormat VulkanHelper::gltfTypeToVkFormat(int type, int componentType, bool normalized) {
    if (normalized) {
        switch (componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R8_UNORM;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R8G8_UNORM;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R8G8B8_UNORM;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R8G8B8A8_UNORM;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R8_SNORM;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R8G8_SNORM;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R8G8B8_SNORM;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R8G8B8A8_SNORM;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R16_UNORM;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R16G16_UNORM;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R16G16B16_UNORM;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R16G16B16A16_UNORM;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R16_SNORM;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R16G16_SNORM;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R16G16B16_SNORM;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R16G16B16A16_SNORM;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            default:
                throw std::runtime_error("component type not implemented");
        }
    } else {
        switch (componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R8_UINT;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R8G8_UINT;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R8G8B8_UINT;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R8G8B8A8_UINT;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            case TINYGLTF_COMPONENT_TYPE_BYTE:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R8_SINT;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R8G8_SINT;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R8G8B8_SINT;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R8G8B8A8_SINT;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R16_UINT;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R16G16_UINT;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R16G16B16_UINT;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R16G16B16A16_UINT;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            case TINYGLTF_COMPONENT_TYPE_SHORT:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R16_SINT;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R16G16_SINT;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R16G16B16_SINT;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R16G16B16A16_SINT;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R32_UINT;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R32G32_UINT;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R32G32B32_UINT;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R32G32B32A32_UINT;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            case TINYGLTF_COMPONENT_TYPE_INT:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R32_SINT;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R32G32_SINT;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R32G32B32_SINT;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R32G32B32A32_SINT;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                switch (type) {
                    case TINYGLTF_TYPE_SCALAR:
                        return VK_FORMAT_R32_SFLOAT;
                    case TINYGLTF_TYPE_VEC2:
                        return VK_FORMAT_R32G32_SFLOAT;
                    case TINYGLTF_TYPE_VEC3:
                        return VK_FORMAT_R32G32B32_SFLOAT;
                    case TINYGLTF_TYPE_VEC4:
                        return VK_FORMAT_R32G32B32A32_SFLOAT;
                    default:
                        throw std::runtime_error("type not implemented");
                }
            default:
                throw std::runtime_error("component type not implemented");
        }
    }
}

uint32_t VulkanHelper::strideFromGltfType(int type, int componentType, size_t stride) {
    if (stride == 0) {
        return tinygltf::GetNumComponentsInType(type) * tinygltf::GetComponentSizeInBytes(componentType);
    } else {
        return stride;
    }
}

VkIndexType VulkanHelper::gltfTypeToVkIndexType(int componentType) {
    switch (componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return VK_INDEX_TYPE_UINT16;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return VK_INDEX_TYPE_UINT32;
        default:
            throw std::runtime_error("index type not implemented");
    }
}

glm::mat4 VulkanHelper::transformFromMatrixOrComponents(std::vector<double> matrix, std::vector<double> scale,
                                                        std::vector<double> rotation, std::vector<double> translation) {
    if (matrix.empty()) {
        glm::mat4 transform = glm::mat4(1.f);

        if (!translation.empty()) {
            glm::vec3 translationVec = glm::make_vec3(translation.data());
            transform = glm::translate(transform, translationVec);
        }

        if (!rotation.empty()) {
            glm::quat rotationQuat = glm::make_quat(rotation.data());
            transform *= glm::mat4_cast(rotationQuat);
        }

        if (!scale.empty()) {
            glm::vec3 scaleVec = glm::make_vec3(scale.data());
            transform = glm::scale(transform, scaleVec);
        }

        return transform;
    } else {
        return glm::make_mat4(matrix.data());
    }
}