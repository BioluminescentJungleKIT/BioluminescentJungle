#include "PostProcessing.h"
#include "Pipeline.h"
#include "Tonemap.h"
#include "Swapchain.h"
#include "VulkanHelper.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include <vulkan/vulkan_core.h>

PostProcessing::PostProcessing(VulkanDevice *device, Swapchain *swapChain) :
        tonemap(Tonemap(device, swapChain)),
        taa(TAA(device, swapChain, &taaTarget)),
        device(device),
        swapchain(swapChain) {
    taaTarget.init(device, MAX_FRAMES_IN_FLIGHT);
    taaTarget.addAttachment(swapchain->swapChainExtent, POST_PROCESSING_FORMAT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
}

void PostProcessing::setupRenderStages(bool recompileShaders) {
    taa.setupRenderStage(recompileShaders, false);
    taaTarget.createFramebuffers(taa.getRenderPass(), swapchain->swapChainExtent);
    tonemap.setupRenderStage(recompileShaders, true);
}

void PostProcessing::createPipeline(bool recompileShaders) {
    taa.createPipeline(recompileShaders);
    tonemap.createPipeline(recompileShaders);
}


void PostProcessing::recordCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer_T *finalTarget) {
    taa.recordCommandBuffer(commandBuffer, taaTarget.framebuffers[swapchain->currentFrame], false);
    tonemap.recordCommandBuffer(commandBuffer, finalTarget, true);
}

void PostProcessing::setupBuffers() {
    taa.setupBuffers();
    tonemap.setupBuffers();
}

void PostProcessing::updateBuffers() {
    taa.updateBuffers();
    tonemap.updateBuffers();
}

void PostProcessing::createDescriptorSets(VkDescriptorPool pool, const RenderTarget &sourceBuffer,
                                          const RenderTarget &gBuffer) {
    taa.createDescriptorSets(pool, sourceBuffer, gBuffer);
    tonemap.createDescriptorSets(pool, taaTarget, gBuffer);
}

PostProcessing::~PostProcessing() {
    taaTarget.destroyAll();
}

RequiredDescriptors PostProcessing::getNumDescriptors() {
    auto taaDescriptors = taa.getNumDescriptors();
    auto tonemapDescriptors = tonemap.getNumDescriptors();
    return {taaDescriptors.requireUniformBuffers + tonemapDescriptors.requireUniformBuffers,
            taaDescriptors.requireSamplers + tonemapDescriptors.requireSamplers};
}

void PostProcessing::handleResize(const RenderTarget &sourceBuffer, const RenderTarget &gBuffer) {
    taaTarget.destroyAll();
    taaTarget.init(device, MAX_FRAMES_IN_FLIGHT);
    taaTarget.addAttachment(swapchain->swapChainExtent, POST_PROCESSING_FORMAT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
    taaTarget.createFramebuffers(taa.getRenderPass(), swapchain->swapChainExtent);
    taa.handleResize(sourceBuffer, gBuffer);
    tonemap.handleResize(taaTarget, gBuffer);
}
