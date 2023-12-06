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
    taaSyncEvents.resize(MAX_FRAMES_IN_FLIGHT);
    for (auto &taaSyncEvent: taaSyncEvents) {
        VkEventCreateInfo eventCreateInfo{};
        eventCreateInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
        VK_CHECK_RESULT(vkCreateEvent(*device, &eventCreateInfo, nullptr, &taaSyncEvent))
    }
    VK_CHECK_RESULT(vkSetEvent(*device, taaSyncEvents[0]))  // allow first frame to render
}

PostProcessing::~PostProcessing() {
    for (auto &taaSyncEvent: taaSyncEvents) {
        vkDestroyEvent(*device, taaSyncEvent, nullptr);
    }
    taaTarget.destroyAll();
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
    vkCmdWaitEvents(commandBuffer, 1, &taaSyncEvents[swapchain->currentFrame],
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                    0, nullptr, 0, nullptr, 0, nullptr);
    taa.recordCommandBuffer(commandBuffer, taaTarget.framebuffers[swapchain->currentFrame], false);
    vkCmdResetEvent(commandBuffer, taaSyncEvents[swapchain->currentFrame],
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vkCmdSetEvent(commandBuffer, taaSyncEvents[(swapchain->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT],
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
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

VkEvent PostProcessing::getCurrentFrameTAAEvent() {
    return taaSyncEvents[swapchain->currentFrame];
}
