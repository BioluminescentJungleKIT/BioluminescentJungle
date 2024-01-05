#pragma once

#include "Swapchain.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "PostProcessingStep.h"


struct DenoiserUBO {
    // Stupid alignment rules ...
    glm::vec4 weights[25];
    glm::ivec4 offsets[25];
    glm::mat4 inverseP;
    glm::int32 iterationCount;

    glm::float32 albedoSigma = 0.1;
    glm::float32 normalSigma = 0.1;
    glm::float32 positionSigma = 0.1;
};

class Denoiser : public PostProcessingStep<DenoiserUBO> {
public:
    Denoiser(VulkanDevice *pDevice, Swapchain *pSwapchain);
    ~Denoiser();
    std::string getShaderName() override;
    void updateUBOContent() override;

    void updateCamera(glm::mat4 projection);

    void disable() override;
    void enable() override;

    void createRenderPass() override;
    void handleResize(const RenderTarget& sourceBuffer, const RenderTarget& gBuffer) override;
    RequiredDescriptors getNumDescriptors() override;
    void createDescriptorSets(VkDescriptorPool pool,
        const RenderTarget& sourceBuffer, const RenderTarget& gBuffer) override;
    void recordCommandBuffer( VkCommandBuffer commandBuffer, VkFramebuffer target, bool renderImGUI) override;

    static constexpr int NR_TMP_BUFFERS = 2;

    RenderTarget tmpTarget;

    // tmpTargetSets[i][j] has the GBuffer attachments from GBuffer[i] and accColor equal to tmpTarget[j]
    std::array<std::array<VkDescriptorSet, NR_TMP_BUFFERS>, MAX_FRAMES_IN_FLIGHT> tmpTargetSets;

    int32_t iterationCount = 3;
    bool enabled = true;

    void recreateTmpTargets();
    void updateTmpSets(const RenderTarget& gBuffer);
};
