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
    tmpTargetSets = VulkanHelper::createDescriptorSetsFromLayout(
        *device, pool, descriptorSetLayout, MAX_FRAMES_IN_FLIGHT);
    updateSamplerBindings(tmpTarget, gBuffer, tmpTargetSets);
}

void Denoiser::handleResize(
    const RenderTarget& sourceBuffer, const RenderTarget& gBuffer)
{
    PostProcessingStep::handleResize(sourceBuffer, gBuffer);

    recreateTmpTargets();
    tmpTarget.createFramebuffers(renderPass, swapchain->renderSize());
    updateSamplerBindings(tmpTarget, gBuffer, tmpTargetSets);
}

void Denoiser::recreateTmpTargets() {
    tmpTarget.destroyAll();
    tmpTarget.init(device, MAX_FRAMES_IN_FLIGHT);
    tmpTarget.addAttachment(swapchain->renderSize(), POST_PROCESSING_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
}

static void addWRBarrier(VkCommandBuffer commandBuffer, VkImage image) {
    return;
    VkImageMemoryBarrier barrier{};

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.image = image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

static void addRWBarrier(VkCommandBuffer commandBuffer, VkImage image) {
    return;
    VkImageMemoryBarrier barrier{};

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.image = image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

void Denoiser::recordCommandBuffer(
    VkCommandBuffer commandBuffer, VkFramebuffer target, bool renderImGUI)
{
    if (ubo.iterationCount <= 1) {
        // Directly pass forward
        runRenderPass(commandBuffer, target, descriptorSets[swapchain->currentFrame], renderImGUI);
        return;
    }

    // General strategy for this effect: we need to do N iterations.
    // We pass the data (while blurring it) between tmpTarget[0] and tmpTarget[1].
    // The first iteration is copying to tmpTarget[0], and the last iteration copies
    // from tmpTarget[currentlyIn] to the actual target.
    runRenderPass(commandBuffer, tmpTarget.framebuffers[0],
        descriptorSets[swapchain->currentFrame], false);
    addWRBarrier(commandBuffer, tmpTarget.images[0].at(0));

    int iterRemaining = ubo.iterationCount - 1;
    int currentlyIn = 0;
    while (iterRemaining > 1) {

        addRWBarrier(commandBuffer, tmpTarget.images[currentlyIn^1].at(0));
        runRenderPass(commandBuffer, tmpTarget.framebuffers[currentlyIn ^ 1],
            tmpTargetSets[currentlyIn], false);
        addWRBarrier(commandBuffer, tmpTarget.images[currentlyIn^1].at(0));
        currentlyIn ^= 1;
        iterRemaining--;
    }

    runRenderPass(commandBuffer, target, tmpTargetSets[currentlyIn], renderImGUI);
}

void Denoiser::updateCamera(glm::mat4 projection) {
    ubo.inverseP = glm::inverse(projection);
}
