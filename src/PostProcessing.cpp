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

void PostProcessing::setupRenderStagePostProcessing(bool recompileShaders) {
    createPostProcessingAttachment();
    tonemap.setupRenderStage(recompileShaders);
    //createPostProcessingPass();
    swapchain->createFramebuffersForRender(PostProcessingRPass);

    PostProcessingSamplers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        PostProcessingSamplers[i] = VulkanHelper::createSampler(device);
    }

    createPostProcessingSetLayout();
    createPostProcessingPipeline(recompileShaders);
}

void PostProcessing::createPostProcessingPipeline(bool recompileShaders) {
    PipelineParameters params;

    params.shadersList = {
            {VK_SHADER_STAGE_VERTEX_BIT,   "shaders/whole_screen.vert"},
            {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/PostProcessing.frag"},
    };

    params.recompileShaders = recompileShaders;

    // PostProcessing shader uses a hardcoded list of vertices
    params.vertexAttributeDescription = {};
    params.vertexInputDescription = {};
    params.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    params.extent = swapchain->swapChainExtent;

    // One color attachment, no blending enabled for it
    params.blending = {{}};
    params.useDepthTest = false;

    params.descriptorSetLayouts = {PostProcessingSetLayout};
    this->PostProcessingPipeline = std::make_unique<GraphicsPipeline>(device, PostProcessingRPass, 0, params);
}

void PostProcessing::createPostProcessingPass(std::vector<VkAttachmentDescription> attachments,
                                              std::vector<VkSubpassDescription> subpasses,
                                              std::vector<VkSubpassDependency> dependencies) {
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = attachments.size();
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = subpasses.size();
    renderPassInfo.pSubpasses = subpasses.data();
    renderPassInfo.dependencyCount = dependencies.size();
    renderPassInfo.pDependencies = dependencies.data();

    VK_CHECK_RESULT(vkCreateRenderPass(*device, &renderPassInfo, nullptr, &PostProcessingRPass))
}

void PostProcessing::recordPostProcessingCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer target) {
    //todo TAA
    //taa.recordCommandBuffer(commandBuffer, target);

    tonemap.recordCommandBuffer(commandBuffer, target);

    VulkanHelper::setFullViewportScissor(commandBuffer, swapchain->swapChainExtent);
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

}

void PostProcessing::createPostProcessingSetLayout() {
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding PostProcessingpingLayoutBinding{};
    PostProcessingpingLayoutBinding.binding = 1;
    PostProcessingpingLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    PostProcessingpingLayoutBinding.descriptorCount = 1;
    PostProcessingpingLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    PostProcessingpingLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings{samplerLayoutBinding, PostProcessingpingLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(*device, &layoutInfo, nullptr, &PostProcessingSetLayout))
}

void PostProcessing::setupBuffers() {
    ubo.allocate(device, sizeof(PostProcessingpingUBO), 1);
}

void PostProcessing::updateBuffers() {
    PostProcessingpingUBO PostProcessingping{};
    PostProcessingping.exposure = exposure;
    PostProcessingping.gamma = gamma;
    PostProcessingping.mode = PostProcessingpingMode;
    ubo.update(&PostProcessingping, sizeof(PostProcessingping), 0);
}

void PostProcessing::createDescriptorSets(VkDescriptorPool pool, const RenderTarget &sourceBuffer) {
    PostProcessingDescriptorSets = VulkanHelper::createDescriptorSetsFromLayout(*device, pool,
                                                                                PostProcessingSetLayout,
                                                                                MAX_FRAMES_IN_FLIGHT);

    updateSamplerBindings(sourceBuffer);
}

void PostProcessing::updateSamplerBindings(const RenderTarget &sourceBuffer) {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = sourceBuffer.imageViews[i].at(0);
        imageInfo.sampler = PostProcessingSamplers[i];

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = PostProcessingDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;
        descriptorWrite.pNext = NULL;

        VkDescriptorBufferInfo PostProcessingpingBufferInfo{};
        PostProcessingpingBufferInfo.buffer = ubo.buffers[0];
        PostProcessingpingBufferInfo.offset = 0;
        PostProcessingpingBufferInfo.range = sizeof(PostProcessingpingUBO);

        VkWriteDescriptorSet PostProcessingpingDescriptorWrite{};
        PostProcessingpingDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        PostProcessingpingDescriptorWrite.dstSet = PostProcessingDescriptorSets[i];
        PostProcessingpingDescriptorWrite.dstBinding = 1;
        PostProcessingpingDescriptorWrite.dstArrayElement = 0;
        PostProcessingpingDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        PostProcessingpingDescriptorWrite.descriptorCount = 1;
        PostProcessingpingDescriptorWrite.pBufferInfo = &PostProcessingpingBufferInfo;
        PostProcessingpingDescriptorWrite.pImageInfo = nullptr; // Optional
        PostProcessingpingDescriptorWrite.pTexelBufferView = nullptr; // Optional

        std::vector<VkWriteDescriptorSet> descriptorWrites{descriptorWrite, PostProcessingpingDescriptorWrite};
        vkUpdateDescriptorSets(*device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }
}

PostProcessing::~PostProcessing() {
    ubo.destroy(device);
    vkDestroyRenderPass(*device, PostProcessingRPass, nullptr);
    vkDestroyDescriptorSetLayout(*device, PostProcessingSetLayout, nullptr);

    for (auto &sampler: this->PostProcessingSamplers) {
        vkDestroySampler(*device, sampler, nullptr);
    }
}

RequiredDescriptors PostProcessing::getNumDescriptors() {
    return {
            .requireUniformBuffers = MAX_FRAMES_IN_FLIGHT,
            .requireSamplers = MAX_FRAMES_IN_FLIGHT,
    };
}

void PostProcessing::handleResize(const RenderTarget &sourceBuffer) {
    updateSamplerBindings(sourceBuffer);
    createPostProcessingPipeline(false);
}

void PostProcessing::createPostProcessingAttachment() {

}
