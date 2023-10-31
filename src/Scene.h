//
// Created by lars on 28.10.23.
//

#ifndef JUNGLE_SCENE_H
#define JUNGLE_SCENE_H

#include <vulkan/vulkan.h>
#include <vector>
#include <glm/glm.hpp>
#include "tiny_gltf.h"


struct ModelTransform {
    glm::mat4 model;
};

// load glft using loader. provide definitions and functions for creating pipeline and rendering it.
class Scene {
public:
    explicit Scene() = default;
    explicit Scene(std::string filename);

    void setupBuffers(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue);
    void
    setupDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout);
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet globalDescriptorSet);
    void destroyBuffers(VkDevice device);

    std::tuple<std::vector<VkVertexInputAttributeDescription>, std::vector<VkVertexInputBindingDescription>>
    getAttributeAndBindingDescriptions();

    VkVertexInputBindingDescription getVertexBindingDescription(int accessor);

    uint32_t getNumDescriptorSets();

    VkDescriptorSetLayout getDescriptorSetLayout(VkDevice device);

    void destroyDescriptorSetLayout(VkDevice device);

private:
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;

    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> bufferMemories;

    std::map<int, VkVertexInputBindingDescription> vertexBindingDescriptions;

    void renderNode(int nodeIndex, std::vector<int> recursionPath, VkCommandBuffer commandBuffer,
                    VkPipelineLayout pipelineLayout, int maxRecursion, VkDescriptorSet globalDescriptorSet);

    void setupUniformBuffers(int nodeIndex, glm::mat4 oldTransform, VkDevice device, VkPhysicalDevice physicalDevice,
                             VkCommandPool commandPool, VkQueue queue, int maxRecursion,
                             std::vector<int> recursionPath);

    std::map<std::vector<int>, int> transformBuffers;
    uint32_t numModelTransforms = 0;
    VkDescriptorSetLayout sceneDescriptorSetLayout;
    std::vector<VkDescriptorSet> descriptorSets;
    std::map<std::vector<int>, int> descriptorSetsMap;
    std::vector<VkDescriptorSet> bindingDescriptorSets;

};


#endif //JUNGLE_SCENE_H
