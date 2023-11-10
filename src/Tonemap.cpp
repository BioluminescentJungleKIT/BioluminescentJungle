#include "Tonemap.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include "VulkanHelper.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include <vulkan/vulkan_core.h>

Tonemap::Tonemap(VulkanDevice* device, Swapchain* swapChain) {
    this->device = device;
    this->swapchain = swapChain;
}

void Tonemap::setupRenderStageTonemap(bool recompileShaders) {
    createTonemapPass();
    swapchain->createFramebuffersForRender(tonemapRPass);

    tonemapSamplers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        tonemapSamplers[i] = VulkanHelper::createSampler(device);
    }

    createTonemapSetLayout();
    createTonemapPipeline(recompileShaders);
}

void Tonemap::createTonemapPipeline(bool recompileShaders) {
    PipelineParameters params;

    params.shadersList = {
        {VK_SHADER_STAGE_VERTEX_BIT, "shaders/tonemap.vert"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/tonemap.frag"},
    };

    params.recompileShaders = recompileShaders;

    // Tonemap shader uses a hardcoded list of vertices
    params.vertexAttributeDescription = {};
    params.vertexInputDescription = {};
    params.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    params.extent = swapchain->swapChainExtent;

    // One color attachment, no blending enabled for it
    params.blending = {{}};
    params.useDepthTest = false;

    params.descriptorSetLayouts = {tonemapSetLayout};
    this->tonemapPipeline = std::make_unique<GraphicsPipeline>(device, tonemapRPass, 0, params);
}

void Tonemap::createTonemapPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchain->swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK_RESULT(vkCreateRenderPass(*device, &renderPassInfo, nullptr, &tonemapRPass))
}

void Tonemap::recordTonemapCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer target) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = tonemapRPass;
    renderPassInfo.framebuffer = target;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain->swapChainExtent;

    renderPassInfo.clearValueCount = 0;
    renderPassInfo.pClearValues = nullptr;
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemapPipeline->pipeline);
    VulkanHelper::setFullViewportScissor(commandBuffer, swapchain->swapChainExtent);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        tonemapPipeline->layout, 0, 1, &tonemapDescriptorSets[swapchain->currentFrame], 0, nullptr);
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRenderPass(commandBuffer);
}

void Tonemap::createTonemapSetLayout() {
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding tonemappingLayoutBinding{};
    tonemappingLayoutBinding.binding = 1;
    tonemappingLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    tonemappingLayoutBinding.descriptorCount = 1;
    tonemappingLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    tonemappingLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> bindings{samplerLayoutBinding, tonemappingLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();
    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(*device, &layoutInfo, nullptr, &tonemapSetLayout))
}

void Tonemap::setupBuffers() {
    VkDeviceSize tonemappingBufferSize = sizeof(TonemappingUBO);
    VulkanHelper::createBuffer(*device, device->physicalDevice, tonemappingBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               tonemappingUniformBuffer,
                               tonemappingUniformBufferMemory);

    vkMapMemory(*device, tonemappingUniformBufferMemory, 0, tonemappingBufferSize, 0, &tonemappingBufferMapped);
}

void Tonemap::updateBuffers() {
    TonemappingUBO tonemapping{};
    tonemapping.exposure = exposure;
    tonemapping.gamma = gamma;
    tonemapping.mode = tonemappingMode;
    memcpy(tonemappingBufferMapped, &tonemapping, sizeof(tonemapping));
}

void Tonemap::createDescriptorSets(VkDescriptorPool pool, const RenderTarget& sourceBuffer) {
    tonemapDescriptorSets = VulkanHelper::createDescriptorSetsFromLayout(*device, pool,
        tonemapSetLayout, MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = sourceBuffer.imageViews[i].at(0);
        imageInfo.sampler = tonemapSamplers[i];

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = tonemapDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;
        descriptorWrite.pNext = NULL;

        VkDescriptorBufferInfo tonemappingBufferInfo{};
        tonemappingBufferInfo.buffer = tonemappingUniformBuffer;
        tonemappingBufferInfo.offset = 0;
        tonemappingBufferInfo.range = sizeof(TonemappingUBO);

        VkWriteDescriptorSet tonemappingDescriptorWrite{};
        tonemappingDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        tonemappingDescriptorWrite.dstSet = tonemapDescriptorSets[i];
        tonemappingDescriptorWrite.dstBinding = 1;
        tonemappingDescriptorWrite.dstArrayElement = 0;
        tonemappingDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        tonemappingDescriptorWrite.descriptorCount = 1;
        tonemappingDescriptorWrite.pBufferInfo = &tonemappingBufferInfo;
        tonemappingDescriptorWrite.pImageInfo = nullptr; // Optional
        tonemappingDescriptorWrite.pTexelBufferView = nullptr; // Optional

        std::vector<VkWriteDescriptorSet> descriptorWrites{descriptorWrite, tonemappingDescriptorWrite};
        vkUpdateDescriptorSets(*device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }
}

Tonemap::~Tonemap() {
    vkDestroyBuffer(*device, tonemappingUniformBuffer, nullptr);
    vkFreeMemory(*device, tonemappingUniformBufferMemory, nullptr);
    vkDestroyRenderPass(*device, tonemapRPass, nullptr);
    vkDestroyDescriptorSetLayout(*device, tonemapSetLayout, nullptr);

    for (auto& sampler : this->tonemapSamplers) {
        vkDestroySampler(*device, sampler, nullptr);
    }
}

RequiredDescriptors Tonemap::getNumDescriptors() {
    return {
        .requireUniformBuffers = MAX_FRAMES_IN_FLIGHT,
        .requireSamplers = MAX_FRAMES_IN_FLIGHT,
    };
}
