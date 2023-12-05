#include "TAA.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include "VulkanHelper.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
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
    ubo.widht = swapchain->swapChainExtent.width;
    ubo.height = swapchain->swapChainExtent.height;
}

TAA::TAA(VulkanDevice *pDevice, Swapchain *pSwapchain, RenderTarget *pTaaTarget) : PostProcessingStep(pDevice, pSwapchain) {
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
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = taaTarget->imageViews[(frameIndex + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT].at(0);
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
