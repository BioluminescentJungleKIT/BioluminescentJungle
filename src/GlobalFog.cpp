#include "GlobalFog.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include "VulkanHelper.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
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
    ubo.near = *near;
    ubo.far = *far;
}

GlobalFog::GlobalFog(VulkanDevice *pDevice, Swapchain *pSwapchain, float *near, float *far) :
        PostProcessingStep(pDevice, pSwapchain), near(near), far(far) {}
