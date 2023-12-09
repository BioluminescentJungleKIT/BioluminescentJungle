#ifndef TONEMAP_H
#define TONEMAP_H

#include "Swapchain.h"
#include "UniformBuffer.h"

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include "PostProcessingStep.h"
#include <memory>


struct TonemappingUBO {
    glm::float32 exposure;
    glm::float32 gamma;
    glm::int32 mode;
};

/**
 * A helper class which manages tonemapping-related resources
 */
class Tonemap : public PostProcessingStep<TonemappingUBO> {
public:
    Tonemap(VulkanDevice *pDevice, Swapchain *pSwapchain);

    std::string getShaderName() override;

    void updateUBOContent() override;

    void disable();

    void enable();

    int tonemappingMode{2};
    float exposure{0};
    float gamma{2.4};
    bool enabled{true};
};

#endif /* end of include guard: TONEMAP_H */
