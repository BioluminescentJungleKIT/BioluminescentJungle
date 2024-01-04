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
#include "tiny_gltf.h"
#include "PhysicalDevice.h"
#include "imgui.h"


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

struct LoD {
    tinygltf::Mesh mesh;
    float dist_min;
    float dist_max;
};

// We may need multiple pipelines for the various parts of the different meshes in the scene.
// The pipieline description object is used to keep track of all created pipelines.
struct PipelineDescription {
    std::optional<int> vertexPosAccessor;
    std::optional<int> vertexTexcoordsAccessor;
    std::optional<int> vertexFixedColorAccessor;
    std::optional<int> vertexNormalAccessor;

    auto toTuple() const {
        return std::make_tuple(vertexPosAccessor, vertexTexcoordsAccessor, vertexFixedColorAccessor);
    }

    bool operator < (const PipelineDescription& other) const {
        return toTuple() < other.toTuple();
    }

    bool operator == (const PipelineDescription& other) const {
        return toTuple() == other.toTuple();
    }
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
    void computeDefaultCameraPos(glm::vec3& lookAt, glm::vec3& cameraPos, float& fov);

    struct LoadedTexture
    {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView imageView;
        VkSampler sampler;
        VkDescriptorSet dSet;
    };

    void drawPointLights(VkCommandBuffer buffer);
    static bool addArtificialLight;

    void cameraButtons(glm::vec3 &lookAt, glm::vec3 &position, glm::vec3 &up, float &fovy, float &near, float &far);

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

    std::map<std::vector<int>, int> transformBuffers;

    VkDescriptorSetLayout uboDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout textureDescriptorSetLayout{VK_NULL_HANDLE};

    std::vector<VkDescriptorSet> uboDescriptorSets;

    std::map<int, int> buffersMap;
    std::map<int, int> descriptorSetsMap;
    std::map<int, std::vector<ModelTransform>> meshTransforms;
    std::map<std::string, std::vector<LoD>> lods; // map base names to LoDs. if none exist, just use the same
    std::vector<VkDescriptorSet> bindingDescriptorSets;
    std::map<int, LoadedTexture> textures;
    std::vector<LightData> lights;
    int lightsBuffer{-1};
    std::vector<CameraData> cameras;

    void addLoD(tinygltf::Mesh &mesh);
};


#endif //JUNGLE_SCENE_H
