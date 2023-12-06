#ifndef LIGHTIG_H
#define LIGHTIG_H

#include "Scene.h"
#include "Swapchain.h"
#include "UniformBuffer.h"
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include <memory>

#define LIGHT_ACCUMULATION_FORMAT VK_FORMAT_R32G32B32A32_SFLOAT

struct DebugOptions {
    // 0 - don't show light boxes, 1 - show light bbox as an overlay
    glm::int32_t showLightBoxes = 0;

    // 0 - normal, 1 - show albedo, 2 - show depth
    glm::int32_t compositionMode = 0;

    glm::float32_t lightRadius = 1.0;
};

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

    VkRenderPass renderPass;
    std::unique_ptr<GraphicsPipeline> pipeline;
    std::unique_ptr<GraphicsPipeline> debugPipeline;

    void createRenderPass();
    VkDescriptorSetLayout samplersLayout, debugLayout;

    std::vector<VkDescriptorSet> samplersSets;
    std::vector<VkDescriptorSet> debugSets;
    std::vector<std::vector<VkSampler>> samplers;

    void setup(bool recompileshaders, Scene *scene, VkDescriptorSetLayout mvpLayout);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet, Scene *scene);
    void createDescriptorSetLayout();

    void createDescriptorSets(VkDescriptorPool pool, const RenderTarget& gBuffer);

    RenderTarget compositedLight;

    void handleResize(const RenderTarget& gBuffer, VkDescriptorSetLayout mvpSetLayout, Scene *scene);
    void setupRenderTarget();
    void updateSamplerBindings(const RenderTarget& gBuffer);

    void setupBuffers();
    void updateBuffers(glm::mat4 VP, glm::vec3 cameraPos, glm::vec3 cameraUp);

    float lightRadiusLog = 0.5;
    DebugOptions debug;

    RequiredDescriptors getNumDescriptors();

  private:
    VulkanDevice *device;
    Swapchain *swapchain;
    UniformBuffer debugUBO;
    UniformBuffer lightUBO;
};

#endif /* end of include guard: LIGHTIG_H */