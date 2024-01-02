#include "PostProcessing.h"
#include "Pipeline.h"
#include "Tonemap.h"
#include "Swapchain.h"
#include <vulkan/vulkan_core.h>

PostProcessing::PostProcessing(VulkanDevice *device, Swapchain *swapChain) :
        tonemap(Tonemap(device, swapChain)),
        taa(TAA(device, swapChain, &taaTarget)),
        fog(GlobalFog(device, swapChain)),
        device(device),
        swapchain(swapChain) {
    fogTarget.init(device, MAX_FRAMES_IN_FLIGHT);
    fogTarget.addAttachment(swapchain->renderSize(), POST_PROCESSING_FORMAT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
    taaTarget.init(device, MAX_FRAMES_IN_FLIGHT);
    taaTarget.addAttachment(swapchain->finalBufferSize, POST_PROCESSING_FORMAT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
}

PostProcessing::~PostProcessing() {
    taaTarget.destroyAll();
    fogTarget.destroyAll();
}

void PostProcessing::setupRenderStages(bool recompileShaders) {
    fog.setupRenderStage(recompileShaders);
    fogTarget.createFramebuffers(fog.getRenderPass(), swapchain->renderSize());
    taa.setupRenderStage(recompileShaders);
    taaTarget.createFramebuffers(taa.getRenderPass(), swapchain->finalBufferSize);
    tonemap.setupRenderStage(recompileShaders);
}

void PostProcessing::createPipeline(bool recompileShaders) {
    fog.createPipeline(recompileShaders);
    taa.createPipeline(recompileShaders);
    tonemap.createPipeline(recompileShaders);
}

void PostProcessing::recordCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer_T *finalTarget) {
    fog.recordCommandBuffer(commandBuffer, fogTarget.framebuffers[swapchain->currentFrame], false);
    taa.recordCommandBuffer(commandBuffer, taaTarget.framebuffers[swapchain->currentFrame], false);
    tonemap.recordCommandBuffer(commandBuffer, finalTarget, true);
}

void PostProcessing::setupBuffers() {
    fog.setupBuffers();
    taa.setupBuffers();
    tonemap.setupBuffers();
}

void PostProcessing::updateBuffers() {
    fog.updateBuffers();
    taa.updateBuffers();
    tonemap.updateBuffers();
}

void PostProcessing::createDescriptorSets(VkDescriptorPool pool, const RenderTarget &sourceBuffer,
                                          const RenderTarget &gBuffer) {
    fog.createDescriptorSets(pool, sourceBuffer, gBuffer);
    taa.createDescriptorSets(pool, fogTarget, gBuffer);
    tonemap.createDescriptorSets(pool, taaTarget, gBuffer);
}

RequiredDescriptors PostProcessing::getNumDescriptors() {
    auto fogDescriptors = fog.getNumDescriptors();
    auto taaDescriptors = taa.getNumDescriptors();
    auto tonemapDescriptors = tonemap.getNumDescriptors();

    RequiredDescriptors requiredDescriptors{};

    for (auto requirements: {fogDescriptors, taaDescriptors, tonemapDescriptors}) {
        requiredDescriptors.requireUniformBuffers += requirements.requireUniformBuffers;
        requiredDescriptors.requireSamplers += requirements.requireSamplers;
    }

    return requiredDescriptors;
}

void PostProcessing::handleResize(const RenderTarget &sourceBuffer, const RenderTarget &gBuffer) {
    fogTarget.destroyAll();
    fogTarget.init(device, MAX_FRAMES_IN_FLIGHT);
    fogTarget.addAttachment(swapchain->renderSize(), POST_PROCESSING_FORMAT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
    fogTarget.createFramebuffers(fog.getRenderPass(), swapchain->renderSize());
    fog.handleResize(sourceBuffer, gBuffer);

    taaTarget.destroyAll();
    taaTarget.init(device, MAX_FRAMES_IN_FLIGHT);
    taaTarget.addAttachment(swapchain->finalBufferSize, POST_PROCESSING_FORMAT,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
    taaTarget.createFramebuffers(taa.getRenderPass(), swapchain->finalBufferSize);
    taa.handleResize(fogTarget, gBuffer);

    tonemap.handleResize(taaTarget, gBuffer);
}

void PostProcessing::enable() {
    fog.enable();
    taa.enable();
    tonemap.enable();
}

void PostProcessing::disable() {
    fog.disable();
    taa.disable();
    tonemap.disable();
}
