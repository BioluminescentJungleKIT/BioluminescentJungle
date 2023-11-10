#ifndef LIGHTIG_H
#define LIGHTIG_H

#include "Scene.h"
#include "Swapchain.h"
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include <memory>

#define LIGHT_ACCUMULATION_FORMAT VK_FORMAT_R8G8B8A8_SRGB

struct DebugOptions {
    int debugBoxes;
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

    void setupBuffers();
    void updateBuffers();

    DebugOptions debug;

    RequiredDescriptors getNumDescriptors();

  private:
    VulkanDevice *device;
    Swapchain *swapchain;

    std::vector<VkBuffer> debugUniformBuffer;
    std::vector<VkDeviceMemory> debugUniformBufferMemory;
    std::vector<void *> debugBufferMapped;
};

#endif /* end of include guard: LIGHTIG_H */
