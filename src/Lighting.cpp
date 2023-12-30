#include "Lighting.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include "VulkanHelper.h"
#include "GBufferDescription.h"
#include <vulkan/vulkan_core.h>

struct LightingBuffer {
    glm::mat4 inverseMVP;
    alignas(16) glm::vec3 cameraPos;
    alignas(16) glm::vec3 cameraUp;
    glm::float32_t viewportWidth;
    glm::float32_t viewportHeight;
    glm::float32 fogAbsorption;
    glm::float32 scatterStrength;
    glm::float32 lightBleed;
};

DeferredLighting::DeferredLighting(VulkanDevice* device, Swapchain* swapChain) {
    this->device = device;
    this->swapchain = swapChain;
}

DeferredLighting::~DeferredLighting() {
    debugUBO.destroy(device);
    lightUBO.destroy(device);
    vkDestroyRenderPass(*device, renderPass, nullptr);
    vkDestroyDescriptorSetLayout(*device, debugLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, samplersLayout, nullptr);

    for (auto& samplerRow : this->samplers) {
        for (auto& sampler: samplerRow) {
            vkDestroySampler(*device, sampler, nullptr);
        }
    }

    compositedLight.destroyAll();
}

void DeferredLighting::createPipeline(bool recompileShaders, VkDescriptorSetLayout mvpLayout, Scene *scene) {
    PipelineParameters params;
    params.shadersList = {
        {VK_SHADER_STAGE_VERTEX_BIT, "shaders/point-light.vert"},
        {VK_SHADER_STAGE_GEOMETRY_BIT, "shaders/point-light.geom"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/point-light.frag"},
    };

    params.recompileShaders = recompileShaders;

    // Tonemap shader uses a hardcoded list of vertices
    auto [attributes, inputs] = scene->getLightsAttributeAndBindingDescriptions();
    params.vertexAttributeDescription = attributes;
    params.vertexInputDescription = inputs;
    params.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    params.extent = swapchain->swapChainExtent;

    // One color attachment, no blending enabled for it
    params.blending = {BasicBlending{
        .blend = VK_BLEND_OP_ADD,
        .srcBlend = VK_BLEND_FACTOR_ONE,
        .dstBlend = VK_BLEND_FACTOR_ONE,
    }};

    // TODO: depth testing, we ought to enable it
    params.useDepthTest = false;
    params.descriptorSetLayouts = {mvpLayout, samplersLayout, debugLayout};
    this->pipeline = std::make_unique<GraphicsPipeline>(device, renderPass, 0, params);

    // Second pipeline: used for debug purposes
    params.shadersList = {
        {VK_SHADER_STAGE_VERTEX_BIT, "shaders/point-debug.vert"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/point-light.frag"},
    };

    params.recompileShaders = recompileShaders;

    // Tonemap shader uses a hardcoded list of vertices
    params.vertexAttributeDescription = {};
    params.vertexInputDescription = {};
    params.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    params.extent = swapchain->swapChainExtent;

    // One color attachment, no blending enabled for it
    params.blending = {{}};

    // TODO: depth testing, we ought to enable it
    params.useDepthTest = false;
    params.descriptorSetLayouts = {mvpLayout, samplersLayout, debugLayout};
    this->debugPipeline = std::make_unique<GraphicsPipeline>(device, renderPass, 0, params);

    // Compute raytracing pipeline
}

void DeferredLighting::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = LIGHT_ACCUMULATION_FORMAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // We need to first transition the attachments to ATTACHMENT_WRITE, then transition back to READ for the
    // next stages
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = dependencies.size();
    renderPassInfo.pDependencies = dependencies.data();

    VK_CHECK_RESULT(vkCreateRenderPass(*device, &renderPassInfo, nullptr, &renderPass))
}

void DeferredLighting::setup(bool recompileShaders, Scene *scene, VkDescriptorSetLayout mvpLayout) {
    createRenderPass();

    samplers.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        samplers[i].resize(GBufferTarget::NumAttachments);
        for (int j = 0; j < GBufferTarget::NumAttachments; j++) {
            samplers[i][j] = VulkanHelper::createSampler(device, true);
        }
    }

    createDescriptorSetLayout();
    createPipeline(recompileShaders, mvpLayout, scene);
    setupRenderTarget();
}

void DeferredLighting::recordRaytraceBuffer(
    VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet, Scene* scene)
{}

void DeferredLighting::recordRasterBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet, Scene *scene) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = compositedLight.framebuffers[swapchain->currentFrame];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain->swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = clearValues.size();
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    GraphicsPipeline *currentPipeline = useDebugPipeline() ? debugPipeline.get() : pipeline.get();

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->pipeline);

    VulkanHelper::setFullViewportScissor(commandBuffer, swapchain->swapChainExtent);
    std::array<VkDescriptorSet, 3> neededSets = {
        mvpSet, samplersSets[swapchain->currentFrame], debugSets[swapchain->currentFrame],
    };

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        currentPipeline->layout, 0, neededSets.size(), neededSets.data(), 0, nullptr);

    if (useDebugPipeline()) {
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    } else {
        scene->drawPointLights(commandBuffer);
    }

    vkCmdEndRenderPass(commandBuffer);
}

void DeferredLighting::recordCommandBuffer(
    VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet, Scene* scene)
{
    if (useRaytracingPipeline()) {
        recordRaytraceBuffer(commandBuffer, mvpSet, scene);
    } else {
        recordRasterBuffer(commandBuffer, mvpSet, scene);
    }
}

void DeferredLighting::createDescriptorSetLayout() {
    {
        std::array<VkDescriptorSetLayoutBinding, GBufferTarget::NumAttachments> samplers;
        for (int i = 0; i < GBufferTarget::NumAttachments; i++) {
            samplers[i].binding = i;
            samplers[i].descriptorCount = 1;
            samplers[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            samplers[i].pImmutableSamplers = nullptr;
            samplers[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = samplers.size();
        layoutInfo.pBindings = samplers.data();
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(*device, &layoutInfo, nullptr, &samplersLayout));
    }

    {
        std::array<VkDescriptorSetLayoutBinding, 2> debug;
        debug[0].binding = 0;
        debug[0].descriptorCount = 1;
        debug[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        debug[0].pImmutableSamplers = nullptr;
        debug[0].stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        debug[1].binding = 1;
        debug[1].descriptorCount = 1;
        debug[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        debug[1].pImmutableSamplers = nullptr;
        debug[1].stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = debug.size();
        layoutInfo.pBindings = debug.data();
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(*device, &layoutInfo, nullptr, &debugLayout));
    }
}

void DeferredLighting::createDescriptorSets(VkDescriptorPool pool, const RenderTarget& sourceBuffer) {

    this->samplersSets =
        VulkanHelper::createDescriptorSetsFromLayout(*device, pool, samplersLayout, MAX_FRAMES_IN_FLIGHT);
    this->debugSets =
        VulkanHelper::createDescriptorSetsFromLayout(*device, pool, debugLayout, MAX_FRAMES_IN_FLIGHT);

    updateSamplerBindings(sourceBuffer);
}

void DeferredLighting::updateSamplerBindings(const RenderTarget& gBuffer) {
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::vector<VkWriteDescriptorSet> descriptorWrites((int)GBufferTarget::NumAttachments);
        std::vector<VkDescriptorImageInfo> imageInfos((int)GBufferTarget::NumAttachments);

        for (size_t j = 0; j < descriptorWrites.size(); j++) {
            imageInfos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[j].imageView = gBuffer.imageViews[i][j];
            imageInfos[j].sampler = samplers[i][j];

            descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j].dstSet = samplersSets[i];
            descriptorWrites[j].dstBinding = j;
            descriptorWrites[j].dstArrayElement = 0;
            descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[j].descriptorCount = 1;
            descriptorWrites[j].pImageInfo = &imageInfos[j];
            descriptorWrites[j].pNext = NULL;
        }

        VkDescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = debugUBO.buffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(DebugOptions);

        VkWriteDescriptorSet debugWrite{};
        debugWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        debugWrite.dstSet = debugSets[i];
        debugWrite.dstBinding = 0;
        debugWrite.dstArrayElement = 0;
        debugWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        debugWrite.descriptorCount = 1;
        debugWrite.pBufferInfo = &bufferInfo;
        descriptorWrites.push_back(debugWrite);

        VkDescriptorBufferInfo bufferInfoInvTr;
        bufferInfoInvTr.buffer = lightUBO.buffers[i];
        bufferInfoInvTr.offset = 0;
        bufferInfoInvTr.range = sizeof(LightingBuffer);

        VkWriteDescriptorSet debugWriteInvTr{};
        debugWriteInvTr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        debugWriteInvTr.dstSet = debugSets[i];
        debugWriteInvTr.dstBinding = 1;
        debugWriteInvTr.dstArrayElement = 0;
        debugWriteInvTr.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        debugWriteInvTr.descriptorCount = 1;
        debugWriteInvTr.pBufferInfo = &bufferInfoInvTr;
        descriptorWrites.push_back(debugWriteInvTr);

        vkUpdateDescriptorSets(*device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }
}

void DeferredLighting::setupBuffers() {
    debugUBO.allocate(device, sizeof(DebugOptions), MAX_FRAMES_IN_FLIGHT);
    lightUBO.allocate(device, sizeof(LightingBuffer), MAX_FRAMES_IN_FLIGHT);
}

void DeferredLighting::updateBuffers(glm::mat4 vp, glm::vec3 cameraPos, glm::vec3 cameraUp) {
    debug.lightRadius = std::exp(lightRadiusLog);
    debugUBO.update(&debug, sizeof(DebugOptions), swapchain->currentFrame);

    LightingBuffer buffer;
    buffer.inverseMVP = glm::inverse(vp);
    buffer.cameraPos = cameraPos;
    buffer.cameraUp = cameraUp;
    buffer.viewportWidth = swapchain->swapChainExtent.width;
    buffer.viewportHeight = swapchain->swapChainExtent.height;
    buffer.fogAbsorption = *fogAbsorption;
    buffer.scatterStrength = scatterStrength;
    buffer.lightBleed = lightBleed;
    lightUBO.update(&buffer, sizeof(buffer), swapchain->currentFrame);
}

RequiredDescriptors DeferredLighting::getNumDescriptors() {
    return {
        .requireUniformBuffers = MAX_FRAMES_IN_FLIGHT,
        .requireSamplers = MAX_FRAMES_IN_FLIGHT * GBufferTarget::NumAttachments,
    };
}

void DeferredLighting::handleResize(const RenderTarget& gBuffer, VkDescriptorSetLayout mvpSetLayout, Scene *scene) {
    compositedLight.destroyAll();
    setupRenderTarget();
    updateSamplerBindings(gBuffer);
    createPipeline(false, mvpSetLayout, scene);
}

void DeferredLighting::setupRenderTarget() {
    compositedLight.init(device, MAX_FRAMES_IN_FLIGHT);
    compositedLight.addAttachment(swapchain->swapChainExtent, LIGHT_ACCUMULATION_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    compositedLight.createFramebuffers(renderPass, swapchain->swapChainExtent);
}
