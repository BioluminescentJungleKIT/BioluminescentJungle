// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#include "TAA.h"
#include "PostProcessingStep.h"
#include "Swapchain.h"
#include <vulkan/vulkan_core.h>

std::string TAA::getShaderName() {
    return "taa";
}

void TAA::disable() {
    enabled = false;
}

void TAA::enable() {
    enabled = true;
}

void TAA::updateUBOContent() {
    ubo.alpha = enabled ? alpha : 1;
    ubo.mode = enabled ? mode : 0;
    ubo.width = getViewport().width;
    ubo.height = getViewport().height;
}

TAA::TAA(VulkanDevice *pDevice, Swapchain *pSwapchain)
    : PostProcessingStep(pDevice, pSwapchain, PPSTEP_RENDER_FULL_RES) {
}

void TAA::setPTarget(RenderTarget* pTaaTarget) {
    taaTarget = pTaaTarget;
}

unsigned int TAA::getAdditionalSamplersCount() {
    return 1;
}

void
TAA::getAdditionalSamplers(std::vector<VkWriteDescriptorSet> &descriptorWrites,
                           std::vector<VkDescriptorImageInfo> &imageInfos, int frameIndex,
                           const RenderTarget &sourceBuffer) {
    VkDescriptorImageInfo imageInfo{};

    // On the first frame, the image is in VK_IMAGE_LAYOUT_UNDEFINED.
    // We force transition to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL to avoid validation errors
    int prevIdx = (frameIndex + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;
    // Format isn't important for this call
    device->transitionImageLayout(taaTarget->images[prevIdx].at(0), VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = taaTarget->imageViews[prevIdx].at(0);
    imageInfo.sampler = samplers[frameIndex][5];
    imageInfos.push_back(imageInfo);

    VkWriteDescriptorSet descriptorWriteSampler{};
    descriptorWriteSampler.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWriteSampler.dstSet = descriptorSets[frameIndex];
    descriptorWriteSampler.dstBinding = 6;
    descriptorWriteSampler.dstArrayElement = 0;
    descriptorWriteSampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWriteSampler.descriptorCount = 1;
    descriptorWriteSampler.pImageInfo = imageInfos.data();
    descriptorWriteSampler.pNext = NULL;

    descriptorWrites.push_back(descriptorWriteSampler);
}
