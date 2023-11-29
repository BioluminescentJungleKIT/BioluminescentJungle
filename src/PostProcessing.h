#ifndef POSTPROCESSING_H
#define POSTPROCESSING_H

#include "Swapchain.h"
#include "UniformBuffer.h"
#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include "Tonemap.h"
#include <memory>


struct PostProcessingpingUBO {
    glm::float32 exposure;
    glm::float32 gamma;
    glm::int32 mode;
};

/**
 * A helper class which manages PostProcessingping-related resources
 */
class PostProcessing {
  public:
    PostProcessing(VulkanDevice* device, Swapchain* swapChain);
    ~PostProcessing();

    float exposure{0};
    float gamma{2.4};

    // Will also destroy any old pipeline which exists
    void createPostProcessingPipeline(bool recompileShaders);

    VkRenderPass PostProcessingRPass;
    std::unique_ptr<GraphicsPipeline> PostProcessingPipeline;

    void createPostProcessingPass(std::vector<VkAttachmentDescription> attachments,
                                  std::vector<VkSubpassDescription> subpasses,
                                  std::vector<VkSubpassDependency> dependencies);
    VkDescriptorSetLayout PostProcessingSetLayout;

    std::vector<VkDescriptorSet> PostProcessingDescriptorSets;
    std::vector<VkSampler> PostProcessingSamplers;
    int PostProcessingpingMode{2};
    void setupRenderStagePostProcessing(bool recompileshaders);
    void recordPostProcessingCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer target);
    void createPostProcessingSetLayout();

    void handleResize(const RenderTarget& sourceBuffer);
    void updateSamplerBindings(const RenderTarget& sourceBuffer);

    void setupBuffers();
    void updateBuffers();

    void createDescriptorSets(VkDescriptorPool pool, const RenderTarget& sourceBuffer);

    RequiredDescriptors getNumDescriptors();

  private:
    VulkanDevice *device;
    Swapchain *swapchain;
    UniformBuffer ubo;

    void createPostProcessingAttachment();

    VkAttachmentDescription colorAttachment;
    VkAttachmentReference colorAttachmentRef;
    Tonemap tonemap;
};

#endif /* end of include guard: POSTPROCESSING_H */
