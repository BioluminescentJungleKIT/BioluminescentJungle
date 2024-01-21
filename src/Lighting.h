#ifndef LIGHTIG_H
#define LIGHTIG_H

#include "Denoiser.h"
#include "Scene.h"
#include "Swapchain.h"
#include "UniformBuffer.h"
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include <memory>
#include <random>

#define LIGHT_ACCUMULATION_FORMAT VK_FORMAT_R32G32B32A32_SFLOAT

struct DebugOptions {
    // 0 - don't show light boxes, 1 - show light bbox as an overlay
    glm::int32_t showLightBoxes = 0;

    // 0 - normal, 1 - show albedo, 2 - show depth
    glm::int32_t compositionMode = 0;

    glm::float32_t lightRadius = 1.0;
};

class BVH;

/**
 * A class which encapsulates state related to accumulation of light on a framebuffer.
 * It takes the G-Buffer as input and outputs in HDR space, which then JungleApp feeds to the tonemapping pass.
 */
class DeferredLighting {
  public:
    DeferredLighting(VulkanDevice* device, Swapchain* swapChain);
    ~DeferredLighting();

    // Will also destroy any old pipeline which exists
    void createPipeline(bool recompileShaders, VkDescriptorSetLayout mvpLayout, Scene *scene);

    VkRenderPass debugRenderPass, restirFogRenderPass;
    std::unique_ptr<GraphicsPipeline> pointLightsPipeline;
    std::unique_ptr<GraphicsPipeline> visualizationPipeline;
    std::unique_ptr<GraphicsPipeline> restirFogPipeline;
    std::unique_ptr<ComputePipeline> raytracingPipeline;
    std::unique_ptr<ComputePipeline> restirEvalPipeline;
    std::unique_ptr<BVH> bvh;

    VkRenderPass createRenderPass(bool clearCompositedLight);

    void createRenderPass();
    VkDescriptorSetLayout samplersLayout, debugLayout, computeLayout, restirEvalLayout;

    std::vector<VkDescriptorSet> samplersSets;
    std::vector<VkDescriptorSet> debugSets;
    std::vector<VkDescriptorSet> computeSets;

    std::vector<std::vector<VkImageMemoryBarrier>> preComputeBarriers;
    std::vector<std::vector<VkImageMemoryBarrier>> postComputeBarriers;
    void setupBarriers(const RenderTarget& gBuffer);

    VkSampler linearSampler;

    void setup(bool recompileshaders, Scene *scene, VkDescriptorSetLayout mvpLayout);

    void recordCommandBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet, Scene* scene);

    void createDescriptorSetLayout();

    void createDescriptorSets(VkDescriptorPool pool, const RenderTarget& gBuffer, Scene *scene);

    RenderTarget compositedLight;
    RenderTarget finalLight;

    void handleResize(const RenderTarget& gBuffer, VkDescriptorSetLayout mvpSetLayout, Scene *scene);
    void setupRenderTarget();
    void updateDescriptors(const RenderTarget& gBuffer, Scene *scene);

    void setupBuffers();
    void updateBuffers(glm::mat4 VP, glm::vec3 cameraPos, glm::vec3 cameraUp);

    float lightRadiusLog = 0.5;
    DebugOptions debug;

    RequiredDescriptors getNumDescriptors();

    float *fogAbsorption;
    float lightBleed{0.1};
    float scatterStrength{0.05};
    int computeLightAlgo = 0;
    float restirTemporalFactor = 5;
    float restirSpatialFactor = 1;
    int restirSpatialRadius = 4;
    int restirSpatialNeighbors = 4;
    int restirInitialSamples = 32;

    bool useRaytracingPipeline() {
        return debug.compositionMode == 0;
    }

    bool useRasterPipeline() {
        return debug.compositionMode == 7;
    }

    bool useDebugPipeline() {
        return !useRaytracingPipeline() && !useRasterPipeline();
    }

    inline int lastFrame() {
        return lastFrame(swapchain->currentFrame);
    }

    inline int lastFrame(int idx) {
        return (idx + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;
    }

    inline int curFrame() {
        return swapchain->currentFrame;
    }

    Denoiser *getDenoiser() {
        return &denoiser;
    }

  private:
    VulkanDevice *device;
    Swapchain *swapchain;
    UniformBuffer debugUBO;
    UniformBuffer lightUBO;
    UniformBuffer computeParamsUBO;
    DataBuffer emissiveTriangles;
    Denoiser denoiser;

    // *2 for temporary reservoirs while using temporal and spatial reuse
    std::array<DataBuffer, MAX_FRAMES_IN_FLIGHT> reservoirs;
    std::array<DataBuffer, MAX_FRAMES_IN_FLIGHT> tmpReservoirs;

    std::mt19937 rndGen{std::random_device{}()};

    void recordRasterBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet, Scene *scene, bool fogOnly);
    void recordRaytraceBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet, Scene* scene);
    void updateReservoirs();
    bool needRestirBufferReset = true;
};

#endif /* end of include guard: LIGHTIG_H */
