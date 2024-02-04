// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#ifndef TAA_H
#define TAA_H

#include "Swapchain.h"
#include "UniformBuffer.h"

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include "PostProcessingStep.h"
#include <memory>


struct TAAUBO {
    glm::float32 alpha;
    glm::int32 mode;
    glm::uint width;
    glm::uint height;
};

/**
 * A helper class which manages tonemapping-related resources
 */
class TAA : public PostProcessingStep<TAAUBO> {
public:
    TAA(VulkanDevice *pDevice, Swapchain *pSwapchain);

    void setPTarget(RenderTarget* taaTarget);

    std::string getShaderName() override;

    void updateUBOContent() override;

    unsigned int getAdditionalSamplersCount() override;

    void getAdditionalSamplers(std::vector<VkWriteDescriptorSet> &descriptorWrites,
                               std::vector<VkDescriptorImageInfo> &imageInfos, int frameIndex,
                               const RenderTarget &sourceBuffer) override;

    void disable() override;

    void enable() override;

    float alpha{0.1};
    int mode{1};
    bool enabled{true};
    RenderTarget *taaTarget;
};

#endif /* end of include guard: TAA_H */
