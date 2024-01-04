#include "Lighting.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include "VulkanHelper.h"
#include "GBufferDescription.h"
#include <cmath>
#include <vulkan/vulkan_core.h>
#include "BVH.hpp"
#include <random>

struct LightingBuffer {
    glm::mat4 inverseMVP;
    alignas(16) glm::vec3 cameraPos;
    alignas(16) glm::vec3 cameraUp;
    glm::int32 viewportWidth;
    glm::int32 viewportHeight;
    glm::float32 fogAbsorption;
    glm::float32 scatterStrength;
    glm::float32 lightBleed;
    glm::int32 lightAlgo;
    glm::int32 randomSeed;
    glm::float32 restirTemporalFactor;
    glm::int32 restirSpatialRadius;
    glm::int32 restirSpatialNeighbors;
};

struct ComputeParamsBuffer {
    glm::int32_t nPointLights;
    glm::int32_t nTriangles;
};

DeferredLighting::DeferredLighting(VulkanDevice* device, Swapchain* swapChain) {
    this->device = device;
    this->swapchain = swapChain;
}

DeferredLighting::~DeferredLighting() {
    debugUBO.destroy(device);
    lightUBO.destroy(device);
    computeParamsUBO.destroy(device);

    for (int i = 0; i < reservoirs.size(); i++) {
        reservoirs[i].destroy(device);
    }

    for (int i = 0; i < tmpReservoirs.size(); i++) {
        tmpReservoirs[i].destroy(device);
    }

    vkDestroyRenderPass(*device, renderPass, nullptr);
    vkDestroyDescriptorSetLayout(*device, debugLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, samplersLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, computeLayout, nullptr);

    vkDestroySampler(*device, linearSampler, nullptr);
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
    params.extent = swapchain->renderSize();

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
    params.extent = swapchain->renderSize();

    // One color attachment, no blending enabled for it
    params.blending = {{}};

    // TODO: depth testing, we ought to enable it
    params.useDepthTest = false;
    params.descriptorSetLayouts = {mvpLayout, samplersLayout, debugLayout};
    this->debugPipeline = std::make_unique<GraphicsPipeline>(device, renderPass, 0, params);

    // Compute raytracing pipeline
    ComputePipeline::Parameters p;
    p.source = {VK_SHADER_STAGE_COMPUTE_BIT, "shaders/direct-light.comp"};
    p.recompileShaders = recompileShaders;
    p.descriptorSetLayouts = {samplersLayout, computeLayout, samplersLayout};
    this->raytracingPipeline = std::make_unique<ComputePipeline>(device, p);

    p.source = {VK_SHADER_STAGE_COMPUTE_BIT, "shaders/restir-eval.comp"};
    p.recompileShaders = recompileShaders;
    p.descriptorSetLayouts = {samplersLayout, computeLayout};
    this->restirEvalPipeline = std::make_unique<ComputePipeline>(device, p);
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
    this->bvh = std::make_unique<BVH>(device, scene);

    createRenderPass();
    linearSampler = VulkanHelper::createSampler(device, true);
    createDescriptorSetLayout();
    createPipeline(recompileShaders, mvpLayout, scene);
    setupRenderTarget();
}

size_t roundUpDiv(size_t a, size_t b) {
    return (a + b - 1) / b;
}

VkBufferMemoryBarrier getComputeBarrierWtoX(const DataBuffer& buffer, bool readOnly) {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer.buffer;
    barrier.offset = 0;
    barrier.size = buffer.size;

    if (!readOnly) {
        barrier.dstAccessMask |= VK_ACCESS_SHADER_WRITE_BIT;
    }
    return barrier;
}

static void computePipelineBarrier(VkCommandBuffer commandBuffer,
    const std::vector<VkBufferMemoryBarrier>& barriers)
{
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        barriers.size(), barriers.data(),
        0, nullptr);
}

void DeferredLighting::recordRaytraceBuffer(
    VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet, Scene* scene)
{
    // Transition attachments to GENERAL layout
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        preComputeBarriers[0].size(), preComputeBarriers[swapchain->currentFrame].data());


    // First pass: naive raytracing or ReSTIR reservoir filling

    const auto& bindExecComputePipeline = [&] (const std::unique_ptr<ComputePipeline>& pipeline,
        std::vector<VkDescriptorSet> descriptorSets) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->layout, 0,
            descriptorSets.size(), descriptorSets.data(), 0, 0);

        // TODO: is 16x16 the most efficient?
        vkCmdDispatch(commandBuffer, roundUpDiv(swapchain->renderSize().width, 16),
            roundUpDiv(swapchain->renderSize().height, 16), 1);
    };

    // Wait for previous finalized reservoirs to become available for temporal reuse
    computePipelineBarrier(commandBuffer, {
        getComputeBarrierWtoX(tmpReservoirs[lastFrame()], true),
        getComputeBarrierWtoX(tmpReservoirs[curFrame()], false),
    });

    bindExecComputePipeline(raytracingPipeline,
        { samplersSets[curFrame()], computeSets[curFrame()], samplersSets[lastFrame()] });

    // Second pass: ReSTIR reservoir evaluation
    if (this->computeLightAlgo == 0) {
        // Wait for tmp reservoirs to be filled by first pass, so that we can use them to fill the final pass
        // Also wait for write access to the current finalized buffers (idk whether we need this though)
        computePipelineBarrier(commandBuffer, {
            getComputeBarrierWtoX(tmpReservoirs[curFrame()], true),
            getComputeBarrierWtoX(reservoirs[curFrame()], false)
        });

        bindExecComputePipeline(restirEvalPipeline,
            { samplersSets[swapchain->currentFrame], computeSets[swapchain->currentFrame] });
    }

    // Transition attachments to SHADER_READ_OPTIMAL layout
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        postComputeBarriers[0].size(), postComputeBarriers[swapchain->currentFrame].data());
}

void DeferredLighting::recordRasterBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet, Scene *scene) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = compositedLight.framebuffers[swapchain->currentFrame];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain->renderSize();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = clearValues.size();
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    GraphicsPipeline *currentPipeline = useDebugPipeline() ? debugPipeline.get() : pipeline.get();

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->pipeline);

    VulkanHelper::setFullViewportScissor(commandBuffer, swapchain->renderSize());
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
    std::vector<VkDescriptorSetLayoutBinding> samplers;

    for (int i = 0; i < GBufferTarget::NumAttachments; i++) {
        samplers.push_back(vkutil::createSetLayoutBinding(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT));
    }

    samplersLayout = device->createDescriptorSetLayout(samplers);
    computeLayout = device->createDescriptorSetLayout({
        vkutil::createSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
    });

    debugLayout = device->createDescriptorSetLayout({
        vkutil::createSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
        vkutil::createSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
    });
}

void DeferredLighting::createDescriptorSets(VkDescriptorPool pool, const RenderTarget& sourceBuffer, Scene *scene) {

    this->samplersSets =
        VulkanHelper::createDescriptorSetsFromLayout(*device, pool, samplersLayout, MAX_FRAMES_IN_FLIGHT);
    this->debugSets =
        VulkanHelper::createDescriptorSetsFromLayout(*device, pool, debugLayout, MAX_FRAMES_IN_FLIGHT);
    this->computeSets =
        VulkanHelper::createDescriptorSetsFromLayout(*device, pool, computeLayout, MAX_FRAMES_IN_FLIGHT);

    updateDescriptors(sourceBuffer, scene);
}

void DeferredLighting::setupBarriers() {
    preComputeBarriers.resize(MAX_FRAMES_IN_FLIGHT);
    postComputeBarriers.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        preComputeBarriers[i].resize(1);
        postComputeBarriers[i].resize(1);

        // Transition compositedLight[i] to the correct layout: before the compute pass,
        // UNDEFINED -> GENERAL.
        //
        // After the compute, GENERAL -> READ_ONLY_OPTIMAL
        preComputeBarriers[i].back().sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        preComputeBarriers[i].back().oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        preComputeBarriers[i].back().newLayout = VK_IMAGE_LAYOUT_GENERAL;
        preComputeBarriers[i].back().image = compositedLight.images[i][0];
        preComputeBarriers[i].back().subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        preComputeBarriers[i].back().srcAccessMask = 0;
        preComputeBarriers[i].back().dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        preComputeBarriers[i].back().srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        preComputeBarriers[i].back().dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        postComputeBarriers[i].back() = preComputeBarriers[i].back();
        postComputeBarriers[i].back().oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        postComputeBarriers[i].back().newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        postComputeBarriers[i].back().srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        postComputeBarriers[i].back().dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        // Make sure reservoir buffers are fully updated before starting evaluation pass
    }
}

void DeferredLighting::updateDescriptors(const RenderTarget& gBuffer, Scene *scene) {
    auto [pointLights, pointLightsNum] = scene->getPointLights();
    auto pointLightsBuffer =
        vkutil::createDescriptorBufferInfo(pointLights, 0, pointLightsNum * sizeof(LightData));

    ComputeParamsBuffer computeParams;
    computeParams.nPointLights = pointLightsNum;
    computeParams.nTriangles = bvh->getNTriangles();
    computeParamsUBO.update(&computeParams, sizeof(computeParams), 0);
    auto computeParamsBuffer =
        vkutil::createDescriptorBufferInfo(computeParamsUBO.buffers[0], 0, sizeof(computeParams));

    auto triBuffer = bvh->getTriangleInfo();
    auto bvhBuffer = bvh->getBVHInfo();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::vector<VkWriteDescriptorSet> descriptorWrites;
        std::vector<VkDescriptorImageInfo> imageInfos(GBufferTarget::NumAttachments + 1);

        // SamplerSets
        for (size_t j = 0; j < GBufferTarget::NumAttachments; j++) {
            imageInfos[j] = vkutil::createDescriptorImageInfo(gBuffer.imageViews[i][j], linearSampler);
            descriptorWrites.push_back(vkutil::createDescriptorWriteSampler(imageInfos[j], samplersSets[i], j));
        }

        // computeSets
        imageInfos.back().imageView = compositedLight.imageViews[i][0];
        imageInfos.back().sampler = linearSampler;
        imageInfos.back().imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        auto lightInfo = vkutil::createDescriptorBufferInfo(lightUBO.buffers[i], 0, sizeof(LightingBuffer));

        descriptorWrites.push_back(vkutil::createDescriptorWriteSampler(imageInfos.back(), computeSets[i],
                0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
        descriptorWrites.push_back(vkutil::createDescriptorWriteUBO(lightInfo, computeSets[i], 1));
        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(pointLightsBuffer, computeSets[i], 2));
        descriptorWrites.push_back(vkutil::createDescriptorWriteUBO(computeParamsBuffer, computeSets[i], 3));
        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(triBuffer, computeSets[i], 4));
        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(bvhBuffer, computeSets[i], 5));

        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(tmpReservoirs[lastFrame(i)].getDescriptor(), computeSets[i], 6));
        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(tmpReservoirs[i].getDescriptor(), computeSets[i], 7));
        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(reservoirs[i].getDescriptor(), computeSets[i], 8));

        auto reservoirInfo = vkutil::createDescriptorBufferInfo(reservoirs[i].buffer, 0, reservoirs[i].size);

        // debug, light sets
        auto bufferInfo = vkutil::createDescriptorBufferInfo(debugUBO.buffers[i], 0, sizeof(DebugOptions));
        descriptorWrites.push_back(vkutil::createDescriptorWriteUBO(bufferInfo, debugSets[i], 0));
        descriptorWrites.push_back(vkutil::createDescriptorWriteUBO(lightInfo, debugSets[i], 1));
        vkUpdateDescriptorSets(*device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }

    // Transition all images to read_only_optimal so that the first frame can read the old data.
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (int j = 0; j < GBufferTarget::NumAttachments; j++) {
            device->transitionImageLayout(gBuffer.images[i][j], VK_FORMAT_UNDEFINED,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                j == GBufferTarget::Depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }
}

void DeferredLighting::setupBuffers() {
    debugUBO.allocate(device, sizeof(DebugOptions), MAX_FRAMES_IN_FLIGHT);
    lightUBO.allocate(device, sizeof(LightingBuffer), MAX_FRAMES_IN_FLIGHT);
    computeParamsUBO.allocate(device, sizeof(ComputeParamsBuffer), 1);
    updateReservoirs();
    setupBarriers();
}

void DeferredLighting::updateBuffers(glm::mat4 vp, glm::vec3 cameraPos, glm::vec3 cameraUp) {
    debug.lightRadius = std::exp(lightRadiusLog);
    debugUBO.update(&debug, sizeof(DebugOptions), swapchain->currentFrame);

    LightingBuffer buffer;
    buffer.inverseMVP = glm::inverse(vp);
    buffer.cameraPos = cameraPos;
    buffer.cameraUp = cameraUp;
    buffer.viewportWidth = swapchain->renderSize().width;
    buffer.viewportHeight = swapchain->renderSize().height;
    buffer.fogAbsorption = *fogAbsorption;
    buffer.scatterStrength = scatterStrength;
    buffer.lightBleed = lightBleed;
    buffer.lightAlgo = computeLightAlgo;
    buffer.randomSeed = std::uniform_int_distribution<glm::uint32>(0, UINT32_MAX)(rndGen);
    buffer.restirTemporalFactor = restirTemporalFactor;
    buffer.restirSpatialRadius = restirSpatialRadius;
    buffer.restirSpatialNeighbors = restirSpatialNeighbors;
    lightUBO.update(&buffer, sizeof(buffer), swapchain->currentFrame);
}

RequiredDescriptors DeferredLighting::getNumDescriptors() {
    return {
        .requireUniformBuffers = MAX_FRAMES_IN_FLIGHT * 3,
        .requireSamplers = 2 * MAX_FRAMES_IN_FLIGHT * GBufferTarget::NumAttachments + MAX_FRAMES_IN_FLIGHT,
        .requireSSBOs = MAX_FRAMES_IN_FLIGHT * 5,
    };
}

void DeferredLighting::handleResize(const RenderTarget& gBuffer, VkDescriptorSetLayout mvpSetLayout, Scene *scene) {
    compositedLight.destroyAll();
    setupRenderTarget();
    createPipeline(false, mvpSetLayout, scene);
    updateReservoirs();
    updateDescriptors(gBuffer, scene);
    setupBarriers();
}

void DeferredLighting::setupRenderTarget() {
    compositedLight.init(device, MAX_FRAMES_IN_FLIGHT);
    compositedLight.addAttachment(swapchain->renderSize(), LIGHT_ACCUMULATION_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    compositedLight.createFramebuffers(renderPass, swapchain->renderSize());
}

#define NUM_SAMPLES_PER_RESERVOIR 4

void DeferredLighting::updateReservoirs() {
    struct Reservoir {
        glm::int32 selected[NUM_SAMPLES_PER_RESERVOIR];
        glm::float32 sumW[NUM_SAMPLES_PER_RESERVOIR];
        glm::float32 pHat[NUM_SAMPLES_PER_RESERVOIR];
        glm::int32 totalNumSamples;
    };

    size_t numReservoirs = swapchain->renderSize().width * swapchain->renderSize().height;

    const auto& update = [&] (DataBuffer& reservoir) {
        if (reservoir.size > 0) {
            reservoir.destroy(device);
        }

        reservoir.uploadData(device, NULL, numReservoirs * sizeof(Reservoir),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

        // Zero-Fill reservoir, so that temporal reuse works correctly
        auto cmdBuffer = device->beginSingleTimeCommands();
        vkCmdFillBuffer(cmdBuffer, reservoir.buffer, 0, reservoir.size, 0);
        device->endSingleTimeCommands(cmdBuffer);
    };

    std::for_each(reservoirs.begin(), reservoirs.end(), update);
    std::for_each(tmpReservoirs.begin(), tmpReservoirs.end(), update);
}
