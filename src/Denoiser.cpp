#include "Denoiser.h"

std::string Denoiser::getShaderName() {
    return "denoiser";
}

void Denoiser::disable() {
    ubo.enabled = 0;
}

void Denoiser::enable() {
    ubo.enabled = 1;
}

void Denoiser::updateUBOContent() {
}

Denoiser::Denoiser(VulkanDevice *pDevice, Swapchain *pSwapchain) :
        PostProcessingStep(pDevice, pSwapchain, 0) {}

void Denoiser::denoiserImGUI() {

}
