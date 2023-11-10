#ifndef TONEMAP_H
#define TONEMAP_H

#include "Swapchain.h"
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include <memory>


struct TonemappingUBO {
    glm::float32 exposure;
    glm::float32 gamma;
    glm::int32 mode;
};

/**
 * A helper class which manages tonemapping-related resources
 */
class Tonemap {
  public:
    Tonemap(VulkanDevice* device, Swapchain* swapChain);
    ~Tonemap();

    float exposure{0};
    float gamma{2.4};

    // Will also destroy any old pipeline which exists
    void createTonemapPipeline(bool recompileShaders);

    VkRenderPass tonemapRPass;
    std::unique_ptr<GraphicsPipeline> tonemapPipeline;

    void createTonemapPass();
    VkDescriptorSetLayout tonemapSetLayout;

    VkBuffer tonemappingUniformBuffer;
    VkDeviceMemory tonemappingUniformBufferMemory;
    void * tonemappingBufferMapped;

    std::vector<VkDescriptorSet> tonemapDescriptorSets;
    std::vector<VkSampler> tonemapSamplers;
    int tonemappingMode{2};
    void setupRenderStageTonemap(bool recompileshaders);
    void recordTonemapCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer target);
    void createTonemapSetLayout();

    void setupBuffers();
    void updateBuffers();

    void createDescriptorSets(VkDescriptorPool pool, const RenderTarget& sourceBuffer);

  private:
    VulkanDevice *device;
    Swapchain *swapchain;
};

#endif /* end of include guard: TONEMAP_H */
