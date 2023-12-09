#include "Tonemap.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include "VulkanHelper.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
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

Tonemap::Tonemap(VulkanDevice *pDevice, Swapchain *pSwapchain) : PostProcessingStep(pDevice, pSwapchain) {

}
