// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#ifndef POSTPROCESSING_H
#define POSTPROCESSING_H

#include "Swapchain.h"

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include "Tonemap.h"
#include "TAA.h"
#include "GlobalFog.h"

/**
 * A helper class which manages PostProcessingping-related resources
 */
class PostProcessing {
public:
    PostProcessing(VulkanDevice *device, Swapchain *swapChain);

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

    Tonemap *getTonemappingPointer() {
        return &tonemap;
    }

    TAA *getTAAPointer() {
        return &taa;
    }

    GlobalFog *getFogPointer() {
        return &fog;
    }

    VkRenderPass getFinalRenderPass() {
        return tonemap.getRenderPass();
    }

    void enable();

    void disable();

private:
    VulkanDevice *device;
    Swapchain *swapchain;

    GlobalFog fog;
    Tonemap tonemap;
    TAA taa;

    struct StepInfo {
        PostProcessingStepBase *algorithm;
        RenderTarget target = {};
        bool useRenderSize = true;
        bool isFinal = false;

        VkExtent2D getTargetSize(Swapchain *swapchain) {
            return useRenderSize ? swapchain->renderSize() : swapchain->finalBufferSize;
        }
    };

    std::vector<StepInfo> steps;
};

#endif /* end of include guard: POSTPROCESSING_H */
