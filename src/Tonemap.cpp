#include "Tonemap.h"
#include "Swapchain.h"
#include <vulkan/vulkan_core.h>

std::string Tonemap::getShaderName() {
    return "tonemap";
}

void Tonemap::disable() {
    enabled = false;
}

void Tonemap::enable() {
    enabled = true;
}

void Tonemap::updateUBOContent() {
    ubo.mode = enabled ? tonemappingMode : 0;
    ubo.exposure = enabled ? exposure : 0;
    ubo.gamma = enabled ? gamma : 1;
}

Tonemap::Tonemap(VulkanDevice *pDevice, Swapchain *pSwapchain) :
    PostProcessingStep(pDevice, pSwapchain, PPSTEP_RENDER_LAST | PPSTEP_RENDER_FULL_RES) {
}
