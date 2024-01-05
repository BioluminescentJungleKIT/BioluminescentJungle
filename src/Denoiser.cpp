#include "Denoiser.h"

std::string Denoiser::getShaderName() {
    return "denoiser";
}

void Denoiser::disable() {
    enabled = false;
}

void Denoiser::enable() {
    enabled = true;
}

void Denoiser::updateUBOContent() {
    this->ubo.iterationCount = enabled ? iterationCount : 0;
}

Denoiser::Denoiser(VulkanDevice *pDevice, Swapchain *pSwapchain) : PostProcessingStep(pDevice, pSwapchain, 0) {
    const float w[5] = {1.0/16.0, 1.0/4.0, 3.0/8.0, 1.0/4.0, 1.0/16.0};
    int idx = 0;
    for (int dx = -2; dx <= 2; dx++) {
        for (int dy = -2; dy <= 2; dy++) {
            ubo.weights[idx].r = w[dx + 2] * w[dy + 2];
            ubo.offsets[idx] = {dx, dy, 0, 0};
            ++idx;
        }
    }

    // Update stuff needed for temporary pass
    recreateTmpTargets();
}

Denoiser::~Denoiser() {
    tmpTarget.destroyAll();
}

RequiredDescriptors Denoiser::getNumDescriptors() {
    auto req = PostProcessingStep::getNumDescriptors();
    req.requireSamplers *= 2;
    req.requireUniformBuffers *= 2;
    return req;
}

void Denoiser::createRenderPass() {
    PostProcessingStep::createRenderPass();
    tmpTarget.createFramebuffers(renderPass, swapchain->renderSize());
}

void Denoiser::createDescriptorSets(VkDescriptorPool pool,
    const RenderTarget& sourceBuffer, const RenderTarget& gBuffer)
{
    PostProcessingStep::createDescriptorSets(pool, sourceBuffer, gBuffer);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (int j = 0; j < NR_TMP_BUFFERS; j++) {
            tmpTargetSets[i][j] =
                VulkanHelper::createDescriptorSetsFromLayout(*device, pool, descriptorSetLayout, 1)[0];
        }
    }

    updateTmpSets(gBuffer);
}

void Denoiser::updateTmpSets(const RenderTarget& gBuffer) {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (int j = 0; j < NR_TMP_BUFFERS; j++) {
            std::vector<VkWriteDescriptorSet> writes;
            std::vector<VkDescriptorImageInfo> images;
            images.reserve(1 + GBufferTarget::NumAttachments);

            images.push_back(vkutil::createDescriptorImageInfo(tmpTarget.imageViews[j][0], this->samplers[i][0]));
            writes.push_back(vkutil::createDescriptorWriteSampler(images.back(), tmpTargetSets[i][j], 0));

            auto uboInfo = vkutil::createDescriptorBufferInfo(uniformBuffer.buffers[0], 0, sizeof(DenoiserUBO));
            writes.push_back(vkutil::createDescriptorWriteUBO(uboInfo, tmpTargetSets[i][j], 1));

            for (int k = 0; k < GBufferTarget::NumAttachments; k++) {
                images.push_back(vkutil::createDescriptorImageInfo(gBuffer.imageViews[i][k], samplers[i][k]));
                writes.push_back(vkutil::createDescriptorWriteSampler(images.back(), tmpTargetSets[i][j], k+2));
            }

            vkUpdateDescriptorSets(*device, writes.size(), writes.data(), 0, NULL);
        }
    }
}

void Denoiser::handleResize(
    const RenderTarget& sourceBuffer, const RenderTarget& gBuffer)
{
    PostProcessingStep::handleResize(sourceBuffer, gBuffer);

    recreateTmpTargets();
    tmpTarget.createFramebuffers(renderPass, swapchain->renderSize());
    updateTmpSets(gBuffer);
}

void Denoiser::recreateTmpTargets() {
    tmpTarget.destroyAll();
    tmpTarget.init(device, MAX_FRAMES_IN_FLIGHT);
    tmpTarget.addAttachment(swapchain->renderSize(), POST_PROCESSING_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
}

void Denoiser::recordCommandBuffer(
    VkCommandBuffer commandBuffer, VkFramebuffer target, bool renderImGUI)
{
    if (ubo.iterationCount <= 1) {
        // Directly pass forward
        runRenderPass(commandBuffer, target, descriptorSets[swapchain->currentFrame], renderImGUI);
        return;
    }

    auto& tmpSets = tmpTargetSets[swapchain->currentFrame];

    // General strategy for this effect: we need to do N iterations.
    // We pass the data (while blurring it) between tmpTarget[0] and tmpTarget[1].
    // The first iteration is copying to tmpTarget[0], and the last iteration copies
    // from tmpTarget[currentlyIn] to the actual target.
    runRenderPass(commandBuffer, tmpTarget.framebuffers[0],
        descriptorSets[swapchain->currentFrame], false);
    int iterRemaining = ubo.iterationCount - 1;
    int currentlyIn = 0;

    while (iterRemaining > 1) {
        runRenderPass(commandBuffer, tmpTarget.framebuffers[currentlyIn ^ 1],
            tmpSets[currentlyIn], false);
        currentlyIn ^= 1;
        iterRemaining--;
    }

    runRenderPass(commandBuffer, target, tmpSets[currentlyIn], renderImGUI);
}

void Denoiser::updateCamera(glm::mat4 projection) {
    ubo.inverseP = glm::inverse(projection);
}
