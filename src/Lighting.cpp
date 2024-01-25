#include "Lighting.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include "VulkanHelper.h"
#include "GBufferDescription.h"
#include <cmath>
#include <vulkan/vulkan_core.h>
#include "BVH.hpp"
#include "LightGrid.hpp"
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
    glm::int32 restirInitialSamples;
    glm::int32 restirLightGridRadius;
};

struct ComputeParamsBuffer {
    glm::int32_t nPointLights;
    glm::int32_t nTriangles;
    glm::int32_t nEmissiveTriangles;

    glm::int32 lightGridSizeX;
    glm::int32 lightGridSizeY;
    glm::int32 lightGridOffX;
    glm::int32 lightGridOffY;
    glm::float32 lightGridCellSizeX;
    glm::float32 lightGridCellSizeY;
};

DeferredLighting::DeferredLighting(VulkanDevice* device, Swapchain* swapChain) : denoiser(device, swapChain) {
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

    vkDestroyRenderPass(*device, debugRenderPass, nullptr);
    vkDestroyRenderPass(*device, restirFogRenderPass, nullptr);
    vkDestroyDescriptorSetLayout(*device, debugLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, samplersLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, computeLayout, nullptr);

    vkDestroySampler(*device, linearSampler, nullptr);
    compositedLight.destroyAll();
    finalLight.destroyAll();
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
    this->restirFogPipeline = std::make_unique<GraphicsPipeline>(device, restirFogRenderPass, 0, params);
    this->pointLightsPipeline = std::make_unique<GraphicsPipeline>(device, debugRenderPass, 0, params);

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
    this->visualizationPipeline = std::make_unique<GraphicsPipeline>(device, debugRenderPass, 0, params);

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

VkRenderPass DeferredLighting::createRenderPass(bool clearCompositedLight) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = LIGHT_ACCUMULATION_FORMAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = clearCompositedLight ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout =
        clearCompositedLight ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = dependencies.size();
    renderPassInfo.pDependencies = dependencies.data();

    VkRenderPass renderPass;
    VK_CHECK_RESULT(vkCreateRenderPass(*device, &renderPassInfo, nullptr, &renderPass));
    return renderPass;
}

void DeferredLighting::createRenderPass() {
    debugRenderPass = createRenderPass(true);
    restirFogRenderPass = createRenderPass(false);
}

void DeferredLighting::setup(bool recompileShaders, Scene *scene, VkDescriptorSetLayout mvpLayout) {
    this->lightGrid = std::make_unique<LightGrid>(device, scene, 1, 1);
    this->bvh = std::make_unique<BVH>(device, scene);

    denoiser.setupRenderStage(recompileShaders);

    createRenderPass();
    linearSampler = VulkanHelper::createSampler(device, true);
    createDescriptorSetLayout();
    createPipeline(recompileShaders, mvpLayout, scene);
    setupRenderTarget();
}

size_t roundUpDiv(size_t a, size_t b) {
    return (a + b - 1) / b;
}

VkBufferMemoryBarrier getComputeBarrier(const DataBuffer& buffer, VkAccessFlags srcFlags, VkAccessFlags dstFlags) {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = srcFlags;
    barrier.dstAccessMask = dstFlags;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer.buffer;
    barrier.offset = 0;
    barrier.size = buffer.size;
    return barrier;
}

static void computePipelineBarrier(VkCommandBuffer commandBuffer,
    const std::vector<VkBufferMemoryBarrier>& barriers,
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
{
    vkCmdPipelineBarrier(commandBuffer,
        srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
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
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        preComputeBarriers[0].size(), preComputeBarriers[swapchain->currentFrame].data());

    if (needRestirBufferReset) {
        // At the first frame or after resize, we need to zero-fill the buffers to avoid reuse of stale data.
        // We do this here instead of the resize handler in order to be able to use the same command buffer
        // and avoid more complex synchronization with semaphores.
        std::vector<VkBufferMemoryBarrier> barriers;

        // Zero-Fill reservoirs, so that temporal reuse works correctly
        for (auto& res : reservoirs) {
            vkCmdFillBuffer(commandBuffer, res.buffer, 0, res.size, 0);
            barriers.push_back(getComputeBarrier(res, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT));
        }

        for (auto& res : tmpReservoirs) {
            vkCmdFillBuffer(commandBuffer, res.buffer, 0, res.size, 0);
            barriers.push_back(getComputeBarrier(res, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT));
        }

        computePipelineBarrier(commandBuffer, barriers, VK_PIPELINE_STAGE_TRANSFER_BIT);
        needRestirBufferReset = false;
    }

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
        getComputeBarrier(tmpReservoirs[lastFrame()], VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
        getComputeBarrier(tmpReservoirs[curFrame()], VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
    });

    bindExecComputePipeline(raytracingPipeline,
        { samplersSets[curFrame()], computeSets[curFrame()], samplersSets[lastFrame()] });

    // Second pass: ReSTIR reservoir evaluation
    if (this->computeLightAlgo == 0) {
        // Wait for tmp reservoirs to be filled by first pass, so that we can use them to fill the final pass
        computePipelineBarrier(commandBuffer, {
            getComputeBarrier(tmpReservoirs[curFrame()], VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
        });

        bindExecComputePipeline(restirEvalPipeline,
            { samplersSets[swapchain->currentFrame], computeSets[swapchain->currentFrame] });
    }

    // Transition compositedLight SHADER_READ_ONLY layout for use in the denoise shader
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        postComputeBarriers[0].size(), postComputeBarriers[swapchain->currentFrame].data());
}

void DeferredLighting::recordRasterBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet, Scene *scene, bool fogOnly) {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = fogOnly ? restirFogRenderPass : debugRenderPass;
    renderPassInfo.framebuffer = finalLight.framebuffers[renderPassInfo.renderPass][swapchain->currentFrame];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchain->renderSize();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    renderPassInfo.clearValueCount = clearValues.size();
    renderPassInfo.pClearValues = clearValues.data();
    if (fogOnly) {
        renderPassInfo.clearValueCount = 0;
    }

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    GraphicsPipeline *currentPipeline = useDebugPipeline() ? visualizationPipeline.get() : pointLightsPipeline.get();

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
        denoiser.recordCommandBuffer(commandBuffer,
            finalLight.framebuffers[denoiser.getRenderPass()][swapchain->currentFrame], false);
        recordRasterBuffer(commandBuffer, mvpSet, scene, true);
    } else {
        recordRasterBuffer(commandBuffer, mvpSet, scene, false);
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

        // Triangles, Emissive Triangles, BVH nodes
        vkutil::createSetLayoutBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),

        // Reservoirs
        vkutil::createSetLayoutBinding(7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),

        // Light Grid Structure
        vkutil::createSetLayoutBinding(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
        vkutil::createSetLayoutBinding(11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
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

    denoiser.createDescriptorSets(pool, compositedLight, sourceBuffer);
    updateDescriptors(sourceBuffer, scene);
    setupBarriers(sourceBuffer);
}

void DeferredLighting::setupBarriers(const RenderTarget& gBuffer) {
    preComputeBarriers.resize(MAX_FRAMES_IN_FLIGHT);
    postComputeBarriers.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        preComputeBarriers[i].resize(GBufferTarget::NumAttachments + 1);
        postComputeBarriers[i].resize(1);

        for (int j = 0; j < GBufferTarget::NumAttachments; j++) {
            preComputeBarriers[i][j] = vkutil::createImageBarrier(gBuffer.images[i][j],
                j == GBufferTarget::Depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,

                j == GBufferTarget::Depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT);
        }

        // Transition compositedLight[i] to the correct layout: before the compute pass,
        // UNDEFINED -> GENERAL.
        //
        // After the compute, GENERAL -> READ_ONLY_OPTIMAL
        preComputeBarriers[i].back() = vkutil::createImageBarrier(compositedLight.images[i][0], VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
        postComputeBarriers[i].back() = vkutil::createImageBarrier(compositedLight.images[i][0], VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
}

void DeferredLighting::updateDescriptors(const RenderTarget& gBuffer, Scene *scene) {
    auto [pointLights, butterflyNum, pointLightsNum] = scene->getPointLights();
    int numUsedPointLights = lightGrid->emissiveTriangles.size > 0 ? butterflyNum : pointLightsNum;

    // We lie that we have at least 1 butterfly even if we won't use it because otherwise vulkan complains
    // that we cannot have a range of 0
    auto pointLightsBuffer =
        vkutil::createDescriptorBufferInfo(pointLights, 0, std::max(1, numUsedPointLights) * sizeof(LightData));

    ComputeParamsBuffer computeParams;
    computeParams.nPointLights = numUsedPointLights;
    computeParams.nTriangles = bvh->getNTriangles();
    computeParams.nEmissiveTriangles = lightGrid->emissiveTriangles.size / sizeof(BVH::Triangle);

    computeParams.lightGridCellSizeX = lightGrid->cellSizeX;
    computeParams.lightGridCellSizeY = lightGrid->cellSizeY;
    computeParams.lightGridSizeX = lightGrid->gridSizeX;
    computeParams.lightGridSizeY = lightGrid->gridSizeY;
    computeParams.lightGridOffX = lightGrid->offX;
    computeParams.lightGridOffY = lightGrid->offY;

    computeParamsUBO.update(&computeParams, sizeof(computeParams), 0);
    auto computeParamsBuffer =
        vkutil::createDescriptorBufferInfo(computeParamsUBO.buffers[0], 0, sizeof(computeParams));

    auto triBuffer = bvh->getTriangleInfo();
    auto bvhBuffer = bvh->getBVHInfo();
    auto emiBuffer = lightGrid->emissiveTriangles.getDescriptor();

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
        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(emiBuffer, computeSets[i], 5));
        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(bvhBuffer, computeSets[i], 6));

        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(tmpReservoirs[lastFrame(i)].getDescriptor(), computeSets[i], 7));
        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(tmpReservoirs[i].getDescriptor(), computeSets[i], 8));
        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(reservoirs[i].getDescriptor(), computeSets[i], 9));

        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(lightGrid->gridCellContents.getDescriptor(), computeSets[i], 10));
        descriptorWrites.push_back(vkutil::createDescriptorWriteSBO(lightGrid->gridCellOffsets.getDescriptor(), computeSets[i], 11));

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
    denoiser.setupBuffers();
    debugUBO.allocate(device, sizeof(DebugOptions), MAX_FRAMES_IN_FLIGHT);
    lightUBO.allocate(device, sizeof(LightingBuffer), MAX_FRAMES_IN_FLIGHT);
    computeParamsUBO.allocate(device, sizeof(ComputeParamsBuffer), 1);
    updateReservoirs();
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
    buffer.restirInitialSamples = restirInitialSamples;
    buffer.restirLightGridRadius = restirLightGridRadius;
    lightUBO.update(&buffer, sizeof(buffer), swapchain->currentFrame);
    denoiser.updateBuffers();
}

RequiredDescriptors DeferredLighting::getNumDescriptors() {
    auto req = denoiser.getNumDescriptors();
    req.requireUniformBuffers += MAX_FRAMES_IN_FLIGHT * 3;
    req.requireSamplers += 2 * MAX_FRAMES_IN_FLIGHT * GBufferTarget::NumAttachments + MAX_FRAMES_IN_FLIGHT;
    req.requireSSBOs += MAX_FRAMES_IN_FLIGHT * 5;
    return req;
}

void DeferredLighting::handleResize(const RenderTarget& gBuffer, VkDescriptorSetLayout mvpSetLayout, Scene *scene) {
    compositedLight.destroyAll();
    finalLight.destroyAll();

    setupRenderTarget();
    createPipeline(false, mvpSetLayout, scene);
    updateReservoirs();
    updateDescriptors(gBuffer, scene);
    setupBarriers(gBuffer);

    denoiser.handleResize(compositedLight, gBuffer);
}

void DeferredLighting::setupRenderTarget() {
    compositedLight.init(device, MAX_FRAMES_IN_FLIGHT);
    compositedLight.addAttachment(swapchain->renderSize(), LIGHT_ACCUMULATION_FORMAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    finalLight.init(device, MAX_FRAMES_IN_FLIGHT);
    finalLight.addAttachment(swapchain->renderSize(), LIGHT_ACCUMULATION_FORMAT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    finalLight.createFramebuffers(debugRenderPass, swapchain->renderSize());
    finalLight.createFramebuffers(restirFogRenderPass, swapchain->renderSize());
    finalLight.createFramebuffers(denoiser.getRenderPass(), swapchain->renderSize());
}

#define NUM_SAMPLES_PER_RESERVOIR 4

void DeferredLighting::updateReservoirs() {
    struct Reservoir {
        alignas(16) glm::int32 selected[NUM_SAMPLES_PER_RESERVOIR];
        alignas(16) glm::vec4 positions[NUM_SAMPLES_PER_RESERVOIR];
        alignas(16) glm::float32 sumW[NUM_SAMPLES_PER_RESERVOIR];
        alignas(16) glm::float32 pHat[NUM_SAMPLES_PER_RESERVOIR];
        alignas(16) glm::int32 totalNumSamples;
    };

    size_t numReservoirs = swapchain->renderSize().width * swapchain->renderSize().height;
    needRestirBufferReset = true;

    const auto& update = [&] (DataBuffer& reservoir) {
        if (reservoir.size > 0) {
            reservoir.destroy(device);
        }

        reservoir.uploadData(device, NULL, numReservoirs * sizeof(Reservoir),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    };

    std::for_each(reservoirs.begin(), reservoirs.end(), update);
    std::for_each(tmpReservoirs.begin(), tmpReservoirs.end(), update);
}
