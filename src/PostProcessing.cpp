#include "PostProcessing.h"
#include "Pipeline.h"
#include "Tonemap.h"
#include "Swapchain.h"
#include <vulkan/vulkan_core.h>

PostProcessing::PostProcessing(VulkanDevice *device, Swapchain *swapChain) :
        denoiser(Denoiser(device, swapChain)),
        tonemap(Tonemap(device, swapChain)),
        taa(TAA(device, swapChain)),
        fog(GlobalFog(device, swapChain)),
        device(device),
        swapchain(swapChain) {

    // Generate a list of steps
    this->steps.push_back({
        .algorithm = &denoiser,
    });

    this->steps.push_back({
        .algorithm = &fog,
    });

    this->steps.push_back({
        .algorithm = &taa,
        .useRenderSize = false,
    });

    this->steps.push_back({
        .algorithm = &tonemap,
        .isFinal = true,
    });

    // Set target now, otherwise the vector might move the target to another address if it has to grow
    for (auto& step : steps) {
        if (step.algorithm == &taa) taa.setPTarget(&step.target);
    }

    for (auto& step : steps) {
        if (step.isFinal) continue;
        step.target.init(device, MAX_FRAMES_IN_FLIGHT);
        step.target.addAttachment(step.getTargetSize(swapchain), POST_PROCESSING_FORMAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

PostProcessing::~PostProcessing() {
    for (auto& step : steps) {
        step.target.destroyAll();
    }
}

void PostProcessing::setupRenderStages(bool recompileShaders) {
    for (auto& step : steps) {
        step.algorithm->setupRenderStage(recompileShaders);
        if (!step.isFinal) {
            step.target.createFramebuffers(step.algorithm->getRenderPass(), step.getTargetSize(swapchain));
        }
    }
}

void PostProcessing::createPipeline(bool recompileShaders) {
    for (auto& step : steps) {
        step.algorithm->createPipeline(recompileShaders);
    }
}

void PostProcessing::recordCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer finalTarget) {
    for (auto& step : steps) {
        auto rpass = step.algorithm->getRenderPass();
        step.algorithm->recordCommandBuffer(commandBuffer,
            step.isFinal ? finalTarget : step.target.framebuffers[rpass][swapchain->currentFrame], step.isFinal);
    }
}

void PostProcessing::setupBuffers() {
    for (auto& step : steps) {
        step.algorithm->setupBuffers();
    }
}

void PostProcessing::updateBuffers() {
    for (auto& step : steps) {
        step.algorithm->updateBuffers();
    }
}

void PostProcessing::createDescriptorSets(VkDescriptorPool pool, const RenderTarget &sourceBuffer,
                                          const RenderTarget &gBuffer) {
    for (size_t i = 0; i < steps.size(); i++) {
        steps[i].algorithm->createDescriptorSets(pool,
            i == 0 ? sourceBuffer : steps[i - 1].target, gBuffer);
    }
}

RequiredDescriptors PostProcessing::getNumDescriptors() {
    RequiredDescriptors required{};
    for (auto& step : steps) {
        auto req = step.algorithm->getNumDescriptors();
        required.requireUniformBuffers += req.requireUniformBuffers;
        required.requireSSBOs += req.requireSSBOs;
        required.requireSamplers += req.requireSamplers;
    }
    return required;
}

void PostProcessing::handleResize(const RenderTarget &sourceBuffer, const RenderTarget &gBuffer) {
    for (size_t i = 0; i < steps.size(); i++) {
        if (!steps[i].isFinal) {
            steps[i].target.destroyAll();
            steps[i].target.init(device, MAX_FRAMES_IN_FLIGHT);
            steps[i].target.addAttachment(steps[i].getTargetSize(swapchain), POST_PROCESSING_FORMAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT);
            steps[i].target.createFramebuffers(steps[i].algorithm->getRenderPass(),
                steps[i].getTargetSize(swapchain));
        }

        steps[i].algorithm->handleResize(i == 0 ? sourceBuffer : steps[i - 1].target, gBuffer);
    }
}

void PostProcessing::enable() {
    for (auto& step : steps) {
        step.algorithm->enable();
    }
}

void PostProcessing::disable() {
    for (auto& step : steps) {
        step.algorithm->disable();
    }
}
