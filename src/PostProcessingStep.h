#ifndef POSTPROCESSINGSTEP_H
#define POSTPROCESSINGSTEP_H

#include "Swapchain.h"
#include "UniformBuffer.h"

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Pipeline.h"
#include "VulkanHelper.h"
#include <memory>
#include "GBufferDescription.h"

#define POST_PROCESSING_FORMAT VK_FORMAT_R32G32B32A32_SFLOAT

/**
 * A helper class which manages resources for a post processing step
 */
template<class UBOType>
class PostProcessingStep {
public:
    PostProcessingStep(VulkanDevice *device, Swapchain *swapChain) {
        this->device = device;
        this->swapchain = swapChain;
    };

    ~PostProcessingStep() {
        uniformBuffer.destroy(device);
        vkDestroyRenderPass(*device, renderPass, nullptr);
        vkDestroyDescriptorSetLayout(*device, descriptorSetLayout, nullptr);

        for (auto &samplerRow: this->samplers) {
            for (auto &sampler: samplerRow) {
                vkDestroySampler(*device, sampler, nullptr);
            }
        }
    };

    // Will also destroy any old pipeline which exists
    void createPipeline(bool recompileShaders) {
        PipelineParameters params;

        params.shadersList = {
                {VK_SHADER_STAGE_VERTEX_BIT,   "shaders/whole_screen.vert"},
                {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/" + getShaderName() + ".frag"},
        };

        params.recompileShaders = recompileShaders;

        // PostProcessingStep shader uses a hardcoded list of vertices
        params.vertexAttributeDescription = {};
        params.vertexInputDescription = {};
        params.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        params.extent = swapchain->swapChainExtent;

        // One color attachment, no blending enabled for it
        params.blending = {{}};
        params.useDepthTest = false;

        params.descriptorSetLayouts = {descriptorSetLayout};
        this->pipeline = std::make_unique<GraphicsPipeline>(device, renderPass, 0, params);
    };

    void createRenderPass(bool isFinalPass) {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = isFinalPass ? swapchain->swapChainImageFormat : POST_PROCESSING_FORMAT;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = isFinalPass ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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

        VK_CHECK_RESULT(vkCreateRenderPass(*device, &renderPassInfo, nullptr, &renderPass))
    };

    virtual unsigned int getAdditionalSamplersCount() {
        return 0;
    }

    void setupRenderStage(bool recompileShaders, bool isFinalPass) {
        createRenderPass(isFinalPass);
        if (isFinalPass) {
            swapchain->createFramebuffersForRender(renderPass);
        }

        samplers.resize(MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            samplers[i].resize(GBufferTarget::NumAttachments + 1 + getAdditionalSamplersCount());
            for (int j = 0; j < GBufferTarget::NumAttachments + 1 + getAdditionalSamplersCount(); j++) {
                samplers[i][j] = VulkanHelper::createSampler(device, false);
            }
        }

        createDescriptorSetLayout();
        createPipeline(recompileShaders);
    };

    VkRenderPass getRenderPass() {
        return renderPass;
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer target, bool renderImGUI) {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = target;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchain->swapChainExtent;

        renderPassInfo.clearValueCount = 0;
        renderPassInfo.pClearValues = nullptr;
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        VulkanHelper::setFullViewportScissor(commandBuffer, swapchain->swapChainExtent);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline->layout, 0, 1, &descriptorSets[swapchain->currentFrame], 0, nullptr);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);

        if (renderImGUI) {
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        }

        vkCmdEndRenderPass(commandBuffer);
    };

    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 0;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding bufferLayoutBinding{};
        bufferLayoutBinding.binding = 1;
        bufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bufferLayoutBinding.descriptorCount = 1;
        bufferLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bufferLayoutBinding.pImmutableSamplers = nullptr;

        std::vector<VkDescriptorSetLayoutBinding> bindings{samplerLayoutBinding, bufferLayoutBinding};

        for (int i = 0; i < GBufferTarget::NumAttachments + getAdditionalSamplersCount(); i++) {
            VkDescriptorSetLayoutBinding extraLayoutBinding{};
            extraLayoutBinding.binding = i+2;
            extraLayoutBinding.descriptorCount = 1;
            extraLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            extraLayoutBinding.pImmutableSamplers = nullptr;
            extraLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings.push_back(extraLayoutBinding);
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = bindings.size();
        layoutInfo.pBindings = bindings.data();
        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(*device, &layoutInfo, nullptr, &descriptorSetLayout))

    };

    void handleResize(const RenderTarget &sourceBuffer, const RenderTarget &gBuffer) {
        updateSamplerBindings(sourceBuffer, gBuffer);
        createPipeline(false);
    };

    virtual void
    getAdditionalSamplers(std::vector<VkWriteDescriptorSet> &descriptorWrites,
                          std::vector<VkDescriptorImageInfo> &imageInfos, int frameIndex,
                          const RenderTarget &sourceBuffer) {}

    void updateSamplerBindings(const RenderTarget &sourceBuffer, const RenderTarget &gBuffer) {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = sourceBuffer.imageViews[i].at(0);
            imageInfo.sampler = samplers[i][0];

            VkWriteDescriptorSet descriptorWriteSampler{};
            descriptorWriteSampler.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWriteSampler.dstSet = descriptorSets[i];
            descriptorWriteSampler.dstBinding = 0;
            descriptorWriteSampler.dstArrayElement = 0;
            descriptorWriteSampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWriteSampler.descriptorCount = 1;
            descriptorWriteSampler.pImageInfo = &imageInfo;
            descriptorWriteSampler.pNext = NULL;

            VkDescriptorBufferInfo DescriptorBufferInfo{};
            DescriptorBufferInfo.buffer = uniformBuffer.buffers[0];
            DescriptorBufferInfo.offset = 0;
            DescriptorBufferInfo.range = sizeof(UBOType);

            VkWriteDescriptorSet descriptorWriteBuffer{};
            descriptorWriteBuffer.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWriteBuffer.dstSet = descriptorSets[i];
            descriptorWriteBuffer.dstBinding = 1;
            descriptorWriteBuffer.dstArrayElement = 0;
            descriptorWriteBuffer.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWriteBuffer.descriptorCount = 1;
            descriptorWriteBuffer.pBufferInfo = &DescriptorBufferInfo;
            descriptorWriteBuffer.pImageInfo = nullptr; // Optional
            descriptorWriteBuffer.pTexelBufferView = nullptr; // Optional

            std::vector<VkWriteDescriptorSet> descriptorWrites{descriptorWriteSampler, descriptorWriteBuffer};

            //add gbuffer


            std::vector<VkDescriptorImageInfo> gBufferImageInfos((int) GBufferTarget::NumAttachments);

            for (size_t j = 0; j < gBufferImageInfos.size(); j++) {
                gBufferImageInfos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                gBufferImageInfos[j].imageView = gBuffer.imageViews[i][j];
                gBufferImageInfos[j].sampler = samplers[i][j + 1];

                VkWriteDescriptorSet descriptorWriteGBuffer{};
                descriptorWriteGBuffer.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWriteGBuffer.dstSet = descriptorSets[i];
                descriptorWriteGBuffer.dstBinding = j + 2;
                descriptorWriteGBuffer.dstArrayElement = 0;
                descriptorWriteGBuffer.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWriteGBuffer.descriptorCount = 1;
                descriptorWriteGBuffer.pImageInfo = &gBufferImageInfos[j];
                descriptorWriteGBuffer.pNext = NULL;

                descriptorWrites.push_back(descriptorWriteGBuffer);
            }

            std::vector<VkDescriptorImageInfo> imageInfos;
            getAdditionalSamplers(descriptorWrites, imageInfos, i, sourceBuffer);

            vkUpdateDescriptorSets(*device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
    };

    void setupBuffers() {

        uniformBuffer.allocate(device, sizeof(UBOType), 1);
    };

    void updateBuffers() {
        updateUBOContent();
        uniformBuffer.update(&ubo, sizeof(UBOType), 0);
    };

    void createDescriptorSets(VkDescriptorPool pool, const RenderTarget &sourceBuffer, const RenderTarget &gBuffer) {
        descriptorSets = VulkanHelper::createDescriptorSetsFromLayout(*device, pool, descriptorSetLayout,
                                                                      MAX_FRAMES_IN_FLIGHT);

        updateSamplerBindings(sourceBuffer, gBuffer);
    };

    RequiredDescriptors getNumDescriptors() {
        return {
                .requireUniformBuffers = MAX_FRAMES_IN_FLIGHT,
                .requireSamplers = MAX_FRAMES_IN_FLIGHT,
        };
    };

protected:
    virtual void updateUBOContent() = 0;

    virtual std::string getShaderName() = 0;

    UBOType ubo{};

    std::vector<std::vector<VkSampler>> samplers;
    std::vector<VkDescriptorSet> descriptorSets;
    Swapchain *swapchain;
private:
    VulkanDevice *device;
    UniformBuffer uniformBuffer;

    VkRenderPass renderPass;
    std::unique_ptr<GraphicsPipeline> pipeline;

    VkDescriptorSetLayout descriptorSetLayout;

};

#endif /* end of include guard: POSTPROCESSINGSTEP_H */
