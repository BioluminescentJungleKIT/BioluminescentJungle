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
    glm::uint widht;
    glm::uint height;
};

/**
 * A helper class which manages tonemapping-related resources
 */
class TAA : public PostProcessingStep<TAAUBO> {
public:
    TAA(VulkanDevice *pDevice, Swapchain *pSwapchain, RenderTarget* taaTarget);

    std::string getShaderName() override;

    void updateUBOContent() override;

    unsigned int getAdditionalSamplersCount() override;

    void getAdditionalSamplers(std::vector<VkWriteDescriptorSet> &descriptorWrites,
                               std::vector<VkDescriptorImageInfo> &imageInfos, int frameIndex,
                               const RenderTarget &sourceBuffer) override;

    void disable();

    void enable();

    float alpha{0.1};
    bool enabled{true};
    RenderTarget *taaTarget;
};

#endif /* end of include guard: TAA_H */
