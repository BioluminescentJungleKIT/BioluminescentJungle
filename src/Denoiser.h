#pragma once

#include "Swapchain.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "PostProcessingStep.h"


struct DenoiserUBO {
    glm::int32 enabled = 1;
};

class Denoiser : public PostProcessingStep<DenoiserUBO> {
public:
    Denoiser(VulkanDevice *pDevice, Swapchain *pSwapchain);
    std::string getShaderName() override;
    void updateUBOContent() override;

    void denoiserImGUI();

    void disable() override;
    void enable() override;
};
