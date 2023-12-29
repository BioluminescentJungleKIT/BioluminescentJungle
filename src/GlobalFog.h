#ifndef GLOBALFOG_H
#define GLOBALFOG_H

#include "Swapchain.h"

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "PostProcessingStep.h"


struct GlobalFogUBO {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 inverseVP;

    glm::vec3 color;
    glm::float32 ambientFactor;
    glm::float32 brightness;
    glm::float32 absorption;
    glm::float32 near;
    glm::float32 far;

    glm::float32 viewportWidth;
    glm::float32 viewportHeight;
    glm::float32 ssrStrength;
    glm::float32 ssrHitThreshold;
    glm::float32 ssrEdgeSmoothing;
    glm::int32 ssrRaySteps;
};

/**
 * A helper class which manages tonemapping-related resources
 */
class GlobalFog : public PostProcessingStep<GlobalFogUBO> {
public:
    GlobalFog(VulkanDevice *pDevice, Swapchain *pSwapchain);

    std::string getShaderName() override;

    void updateCamera(glm::mat4 view, glm::mat4 projection, float near, float far);

    void updateUBOContent() override;

    void imguiFog();
    void imguiSSR();

    void disable();

    void enable();

    glm::vec3 color{0.2, 0.03, 0.1};
    glm::float32 ambientFactor{1};
    glm::float32 brightness{0.05};
    glm::float32 absorption{0.2};
    bool enabled{true};

    glm::float32 ssrStrength{1.0};
    glm::float32 ssrHitThreshold{1e-5};
    glm::float32 ssrEdgeSmoothing{1};
    glm::int32 ssrRaySteps{200};
};

#endif /* end of include guard: GLOBALFOG_H */
