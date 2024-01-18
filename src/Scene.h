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
#include "DataBuffer.h"

struct ModelTransform {
    glm::mat4 model;
};
struct LightData {
    glm::vec3 position alignas(16);
    glm::vec3 color alignas(16);
    glm::float32 intensity alignas(16);
    glm::float32 wind alignas(16);
};
struct CameraData{
    std::string name;
    glm::mat4 view;
    float yfov;
    float znear;
    float zfar;
};

struct LodUpdatePushConstants {
    glm::vec4 lodMeta;
    glm::vec3 cameraPosition;
};

struct LoD {
    int mesh;
    float dist_min;
    float dist_max;


    friend bool operator<(const LoD& l, const LoD& r)
    {
        return l.dist_min < r.dist_min || (l.dist_min == r.dist_min && l.dist_max < r.dist_max) || (l.dist_min == r.dist_min && l.dist_max == r.dist_max && l.mesh < r.mesh);
    }

    friend bool operator==(const LoD& l, const LoD& r)
    {
        return l.dist_min == l.dist_min && l.dist_max == r.dist_max && l.mesh == r.mesh;
    }
};

// We may need multiple graphicsPipelines for the various parts of the different meshes in the scene.
// The pipieline description object is used to keep track of all created graphicsPipelines.
struct PipelineDescription {
    std::optional<int> vertexPosAccessor;
    std::optional<int> vertexTexcoordsAccessor;
    std::optional<int> vertexFixedColorAccessor;
    std::optional<int> vertexNormalAccessor;

    bool useSSR = false;
    bool useNormalMap = false;
    bool useDisplacement = false;

    bool isOpaque = true;

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
    glm::float32_t heightScale = 0.002;
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
    void recordCommandBufferCompute(VkCommandBuffer commandBuffer, glm::vec3 cameraPosition);
    void recordCommandBufferDraw(VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet);

    void destroyBuffers();
    std::tuple<std::vector<VkVertexInputAttributeDescription>, std::vector<VkVertexInputBindingDescription>>
    getLightsAttributeAndBindingDescriptions();

    std::pair<VkBuffer, size_t> getPointLights();

    RequiredDescriptors getNumDescriptors();

    void destroyDescriptorSetLayouts();
    void computeDefaultCameraPos(glm::vec3 &lookAt, glm::vec3 &position, glm::vec3 &up, float &fovy, float &near, float &far);

    struct LoadedTexture
    {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView imageView;
        VkSampler sampler;
        VkFormat imageFormat;
    };

    void drawPointLights(VkCommandBuffer buffer);
    void cameraButtons(glm::vec3 &lookAt, glm::vec3 &position, glm::vec3 &up, float &fovy, float &near, float &far);
    void drawImGUIMaterialSettings();

    tinygltf::Model model;
    std::map<int, std::vector<ModelTransform>> meshTransforms;

  private:
    tinygltf::TinyGLTF loader;
    VulkanDevice *device;
    Swapchain *swapchain;

    std::vector<DataBuffer> buffers;

    std::map<PipelineDescription, std::unique_ptr<GraphicsPipeline>> graphicsPipelines;

    // meshPrimitivesWithPipeline[description][meshId] -> list of primitives of the mesh with given pipeline
    std::map<PipelineDescription, std::map<LoD, std::vector<int>>> meshPrimitivesWithPipeline;
    PipelineDescription getPipelineDescriptionForPrimitive(const tinygltf::Primitive& primitive);

    void createPipelinesWithDescription(PipelineDescription descr,
        VkRenderPass renderPass, VkDescriptorSetLayout mvpLayout, bool forceRecompile);

    void renderPrimitiveInstances(int meshId, int primitiveId,
        VkCommandBuffer commandBuffer, const PipelineDescription& descr, VkPipelineLayout pipelineLayout);

    void generateTransforms(int nodeIndex, glm::mat4 oldTransform, int maxRecursion);
    void ensureDescriptorSetLayouts();

    VkVertexInputBindingDescription getVertexBindingDescription(int accessor, int bindingId);
    void setupStorageBuffers();

    VkDescriptorSetLayout meshTransformsDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout materialsSettingsLayout{VK_NULL_HANDLE};

    VkDescriptorSetLayout albedoDSLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout albedoDisplacementDSLayout{VK_NULL_HANDLE};

    VkDescriptorSetLayout lodUpdateDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout lodCompressDescriptorSetLayout{VK_NULL_HANDLE};

    std::vector<VkDescriptorSet> meshTransformsDescriptorSets;
    std::vector<VkDescriptorSet> updateLoDsDescriptorSets;
    std::vector<VkDescriptorSet> compressLoDsDescriptorSets;
    std::vector<VkDescriptorSet> materialSettingSets;

    std::map<int, int> buffersMap;
    std::map<std::pair<int, int>, int> lodTransformsBuffersMap;
    std::map<std::pair<int, int>, int> lodIndirectDrawBufferMap;
    std::map<std::pair<int, int>, int> lodMetaBuffersMap;
    std::map<LoD, int> descriptorSetsMap;
    std::map<std::pair<int, int>, int> lodComputeDescriptorSetsMap;


    std::map<std::string, int> meshNameMap;
    std::map<std::string, std::vector<LoD>> lods; // map base names to LoDs. if none exist, just use the same
    std::vector<VkDescriptorSet> bindingDescriptorSets;
    std::map<int, LoadedTexture> textures;
    std::map<int, VkDescriptorSet> materialDSet;

    std::map<int, int> butterflies;
    std::map<int, LightData> butterflyLights;
    ModelTransform butterflyVolumeTransform;

    std::vector<LightData> lights;
    int lightsBuffer{-1};

    std::vector<CameraData> cameras;
    MaterialSettings materialSettings;
    UniformBuffer materialBuffer;

    UniformBuffer constantsBuffers;

    void addLoD(int meshIndex);
    std::unique_ptr<ComputePipeline> updateLoDsPipeline;

    std::unique_ptr<ComputePipeline> compressLoDsPipeline;

    void setupPrimitiveDrawBuffers();

    unsigned int getNumLods();

    void destroyPipelines();
};


#endif //JUNGLE_SCENE_H
