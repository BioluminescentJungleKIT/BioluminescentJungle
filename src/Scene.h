//
// Created by lars on 28.10.23.
//

#ifndef JUNGLE_SCENE_H
#define JUNGLE_SCENE_H

#include <vulkan/vulkan.h>
#include <vector>
#include "tiny_gltf.h"

// load glft using loader. provide definitions and functions for creating pipeline and rendering it.
class Scene {
public:
    explicit Scene() = default;
    explicit Scene(std::string filename);

    void setupBuffers(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue);
    uint32_t getDescriptorCount();
    void setupDescriptorSets();
    void render(VkCommandBuffer commandBuffer);
    void destroyBuffers(VkDevice device);

    std::tuple<std::vector<VkVertexInputAttributeDescription>, std::vector<VkVertexInputBindingDescription>>
    getAttributeAndBindingDescriptions();

    VkVertexInputBindingDescription getVertexBindingDescription(int accessor);

private:
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;

    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> bufferMemories;

};


#endif //JUNGLE_SCENE_H
