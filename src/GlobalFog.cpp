#include "GlobalFog.h"
#include "Swapchain.h"
#include <vulkan/vulkan_core.h>

std::string GlobalFog::getShaderName() {
    return "global_fog";
}

void GlobalFog::disable() {
    enabled = false;
}

void GlobalFog::enable() {
    enabled = true;
}

void GlobalFog::updateUBOContent() {
    ubo.color = enabled ? color : glm::vec3{0, 0, 0};
    ubo.ambientFactor = ambientFactor;
    ubo.brightness = brightness;
    ubo.absorption = enabled ? absorption : 0;
    ubo.ssrStrength = enabled ? ssrStrength : 0;
    ubo.ssrEdgeSmoothing = ssrEdgeSmoothing;
    ubo.ssrHitThreshold = ssrHitThreshold;
    ubo.ssrRaySteps = ssrRaySteps;
    ubo.renderEmission = enabled;
}

void GlobalFog::updateCamera(glm::mat4 view, glm::mat4 projection, float near, float far) {
    ubo.near = near;
    ubo.far = far;
    ubo.view = view;
    ubo.projection = projection;
    ubo.inverseVP = glm::inverse(projection * view);
    ubo.viewportWidth = swapchain->renderSize().width;
    ubo.viewportHeight = swapchain->renderSize().height;
}

GlobalFog::GlobalFog(VulkanDevice *pDevice, Swapchain *pSwapchain) :
        PostProcessingStep(pDevice, pSwapchain, 0) {}
