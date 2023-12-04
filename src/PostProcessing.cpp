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
        device(device),
        swapchain(swapChain) {
}

void PostProcessing::setupRenderStages(bool recompileShaders) {
    tonemap.setupRenderStage(recompileShaders, true);
    createPipeline(recompileShaders);
}

void PostProcessing::createPipeline(bool recompileShaders) {
    tonemap.createPipeline(recompileShaders);
}

void PostProcessing::recordCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer finalTarget) {
    //todo TAA
    //taa.recordCommandBuffer(commandBuffer, finalTarget);

    tonemap.recordCommandBuffer(commandBuffer, finalTarget, true);
}

void PostProcessing::setupBuffers() {
    tonemap.setupBuffers();
}

void PostProcessing::updateBuffers() {
    tonemap.updateBuffers();
}

void PostProcessing::createDescriptorSets(VkDescriptorPool pool, const RenderTarget &sourceBuffer) {
    tonemap.createDescriptorSets(pool, sourceBuffer);
}

PostProcessing::~PostProcessing() {}

RequiredDescriptors PostProcessing::getNumDescriptors() {
    return tonemap.getNumDescriptors();
}

void PostProcessing::handleResize(const RenderTarget &sourceBuffer) {
    tonemap.handleResize(sourceBuffer);
}
