//
// Created by lars on 28.10.23.
//

#ifndef JUNGLE_SCENE_H
#define JUNGLE_SCENE_H

#include <memory>
#include <vulkan/vulkan.h>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>
#include "Pipeline.h"
#include "Swapchain.h"
#include "UniformBuffer.h"
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
struct CameraData{
    std::string name;
    glm::mat4 view;
    float yfov;
    float znear;
    float zfar;
};

// We may need multiple pipelines for the various parts of the different meshes in the scene.
// The pipieline description object is used to keep track of all created pipelines.
struct PipelineDescription {
    std::optional<int> vertexPosAccessor;
    std::optional<int> vertexTexcoordsAccessor;
    std::optional<int> vertexFixedColorAccessor;
    std::optional<int> vertexNormalAccessor;

    bool useSSR = false;
    bool useNormalMap = false;
    bool useDisplacement = false;

    auto toTuple() const {
        return std::make_tuple(vertexPosAccessor, vertexTexcoordsAccessor, vertexFixedColorAccessor,
            useNormalMap, useDisplacement, useSSR);
    }

    bool operator < (const PipelineDescription& other) const {
        return toTuple() < other.toTuple();
    }

    bool operator == (const PipelineDescription& other) const {
        return toTuple() == other.toTuple();
    }
};

struct MaterialSettings {
    glm::float32_t heightScale = 0.05;
    glm::int32_t raymarchSteps = 100;
    glm::int32_t enableInverseDisplacement = 1;
    glm::int32_t enableLinearApprox = 1;
    glm::int32_t useInvertedFormat = 0;
};

// load glft using loader. provide definitions and functions for creating pipeline and rendering it.
class Scene {
public:
    explicit Scene() = default;

    explicit Scene(VulkanDevice *device, Swapchain *swapchain, std::string filename);

    void createPipelines(VkRenderPass renderPass, VkDescriptorSetLayout mvpLayout, bool forceRecompile);

    // free up all resources
    void destroyAll();

    void setupBuffers();
    void updateBuffers();

    void setupTextures();
    void destroyTextures();

    void setupDescriptorSets(VkDescriptorPool descriptorPool);
    void recordCommandBuffer(
        VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet);

    void destroyBuffers();
    std::tuple<std::vector<VkVertexInputAttributeDescription>, std::vector<VkVertexInputBindingDescription>>
    getLightsAttributeAndBindingDescriptions();

    RequiredDescriptors getNumDescriptors();

    void destroyDescriptorSetLayout();
    void computeDefaultCameraPos(glm::vec3 &lookAt, glm::vec3 &position, glm::vec3 &up, float &fovy, float &near, float &far);

    struct LoadedTexture
    {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView imageView;
        VkSampler sampler;
    };

    void drawPointLights(VkCommandBuffer buffer);
    void cameraButtons(glm::vec3 &lookAt, glm::vec3 &position, glm::vec3 &up, float &fovy, float &near, float &far);
    void drawImGUIMaterialSettings();

  private:
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    VulkanDevice *device;
    Swapchain *swapchain;

    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> bufferMemories;

    std::map<PipelineDescription, std::unique_ptr<GraphicsPipeline>> pipelines;

    // meshPrimitivesWithPipeline[description][meshId] -> list of primitives of the mesh with given pipeline
    std::map<PipelineDescription, std::map<int, std::vector<int>>> meshPrimitivesWithPipeline;
    PipelineDescription getPipelineDescriptionForPrimitive(const tinygltf::Primitive& primitive);

    void createPipelinesWithDescription(PipelineDescription descr,
        VkRenderPass renderPass, VkDescriptorSetLayout mvpLayout, bool forceRecompile);

    void renderPrimitiveInstances(int meshId, int primitiveId,
        VkCommandBuffer commandBuffer, const PipelineDescription& descr, VkPipelineLayout pipelineLayout);

    void generateTransforms(int nodeIndex, glm::mat4 oldTransform, int maxRecursion);
    void ensureDescriptorSetLayouts();

    VkVertexInputBindingDescription getVertexBindingDescription(int accessor, int bindingId);
    void setupUniformBuffers();

    VkDescriptorSetLayout uboDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout materialsSettingsLayout{VK_NULL_HANDLE};

    VkDescriptorSetLayout albedoDSLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout albedoDisplacementDSLayout{VK_NULL_HANDLE};

    std::vector<VkDescriptorSet> uboDescriptorSets;
    std::vector<VkDescriptorSet> materialSettingSets;

    std::map<int, int> buffersMap;
    std::map<int, int> descriptorSetsMap;
    std::map<int, std::vector<ModelTransform>> meshTransforms;
    std::vector<VkDescriptorSet> bindingDescriptorSets;

    std::map<int, LoadedTexture> textures;
    std::map<int, VkDescriptorSet> materialDSet;

    std::vector<LightData> lights;
    int lightsBuffer{-1};
    std::vector<CameraData> cameras;

    MaterialSettings materialSettings;
    UniformBuffer materialBuffer;
    UniformBuffer constantsBuffers;
};


#endif //JUNGLE_SCENE_H
