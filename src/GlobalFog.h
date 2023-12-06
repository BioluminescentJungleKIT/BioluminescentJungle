#ifndef GLOBALFOG_H
#define GLOBALFOG_H

#include "Swapchain.h"
#include "UniformBuffer.h"

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include "PostProcessingStep.h"
#include <memory>


struct GlobalFogUBO {
    glm::vec3 color;
    glm::float32 ambientFactor;
    glm::float32 brightness;
    glm::float32 absorption;
    glm::float32 near;
    glm::float32 far;
};

/**
 * A helper class which manages tonemapping-related resources
 */
class GlobalFog : public PostProcessingStep<GlobalFogUBO> {
public:
    GlobalFog(VulkanDevice *pDevice, Swapchain *pSwapchain, float *near, float *far);

    std::string getShaderName() override;

    void updateUBOContent() override;

    void disable();

    void enable();

    glm::vec3 color{0.2, 0.03, 0.1};
    glm::float32 ambientFactor{1};
    glm::float32 brightness{0.05};
    glm::float32 absorption{0.2};
    bool enabled{true};
    float *near{};
    float *far{};
};

#endif /* end of include guard: GLOBALFOG_H */
