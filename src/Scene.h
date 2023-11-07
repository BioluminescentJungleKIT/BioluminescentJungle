//
// Created by lars on 28.10.23.
//

#ifndef JUNGLE_SCENE_H
#define JUNGLE_SCENE_H

#include <vulkan/vulkan.h>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>
#include "tiny_gltf.h"
#include "PhysicalDevice.h"


struct ModelTransform {
    glm::mat4 model;
};
struct LightData {
    glm::vec3 position;
    glm::vec3 color;
    glm::float32 intensity;
};

// load glft using loader. provide definitions and functions for creating pipeline and rendering it.
class Scene {
public:
    explicit Scene() = default;

    explicit Scene(std::string filename);

    void setupBuffers(VulkanDevice *device);

    void setupTextures(VulkanDevice* device);
    void destroyTextures(VulkanDevice* device);
    std::string queryShaderName();

    void
    setupDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool);

    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet globalDescriptorSet);

    void destroyBuffers(VkDevice device);

    std::tuple<std::vector<VkVertexInputAttributeDescription>, std::vector<VkVertexInputBindingDescription>>
    getAttributeAndBindingDescriptions();
    std::tuple<std::vector<VkVertexInputAttributeDescription>, std::vector<VkVertexInputBindingDescription>>
    getLightsAttributeAndBindingDescriptions();

    uint32_t getNumDescriptorSets();
    uint32_t getNumTextures();

    std::vector<VkDescriptorSetLayout> getDescriptorSetLayouts(VkDevice device);

    void destroyDescriptorSetLayout(VkDevice device);
    void computeCameraPos(glm::vec3& lookAt, glm::vec3& cameraPos, float& fov);

    struct LoadedTexture
    {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView imageView;
        VkSampler sampler;
        VkDescriptorSet dSet;
    };

  private:
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;

    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> bufferMemories;

    std::map<int, VkVertexInputBindingDescription> vertexBindingDescriptions;

    void renderInstances(int mesh, VkCommandBuffer commandBuffer,
                         VkPipelineLayout pipelineLayout, VkDescriptorSet globalDescriptorSet);

    void generateTransforms(int nodeIndex, glm::mat4 oldTransform, int maxRecursion);

    VkVertexInputBindingDescription getVertexBindingDescription(int accessor, int bindingId);
    void setupUniformBuffers(VulkanDevice *device);

    std::map<std::vector<int>, int> transformBuffers;

    VkDescriptorSetLayout uboDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout textureDescriptorSetLayout{VK_NULL_HANDLE};

    std::vector<VkDescriptorSet> uboDescriptorSets;

    std::map<int, int> buffersMap;
    std::map<int, int> descriptorSetsMap;
    std::map<int, std::vector<ModelTransform>> meshTransforms;
    std::vector<VkDescriptorSet> bindingDescriptorSets;
    std::map<int, LoadedTexture> textures;
    std::vector<LightData> lights;
    int lightsBuffer{-1};
};


#endif //JUNGLE_SCENE_H
