#ifndef POSTPROCESSING_H
#define POSTPROCESSING_H

#include "Swapchain.h"
#include "UniformBuffer.h"
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include "Tonemap.h"
#include "TAA.h"
#include <memory>

/**
 * A helper class which manages PostProcessingping-related resources
 */
class PostProcessing {
  public:
    PostProcessing(VulkanDevice* device, Swapchain* swapChain);
    ~PostProcessing();

    // Will also destroy any old pipeline which exists
    void createPipeline(bool recompileShaders);

    std::vector<VkDescriptorSet> PostProcessingDescriptorSets;
    std::vector<VkSampler> PostProcessingSamplers;
    void setupRenderStages(bool recompileShaders);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer_T *finalTarget);

    void handleResize(const RenderTarget &sourceBuffer, const RenderTarget &gBuffer);

    void setupBuffers();
    void updateBuffers();

    void createDescriptorSets(VkDescriptorPool pool, const RenderTarget &sourceBuffer,
                              const RenderTarget &gBuffer);

    RequiredDescriptors getNumDescriptors();

    Tonemap* getTonemappingPointer() {
        return &tonemap;
    }

    TAA* getTAAPointer() {
        return &taa;
    }

    VkRenderPass getFinalRenderPass() {
        return tonemap.getRenderPass();
    }

  private:
    VulkanDevice *device;
    Swapchain *swapchain;

    Tonemap tonemap;
    TAA taa;

    RenderTarget taaTarget;
};

#endif /* end of include guard: POSTPROCESSING_H */
