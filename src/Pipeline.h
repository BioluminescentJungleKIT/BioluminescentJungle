#ifndef PIPELINE_H
#define PIPELINE_H

#include "PhysicalDevice.h"
#include <string>
#include <vulkan/vulkan_core.h>

/**
 * A class which makes it easier to manage pipelines.
 */

struct BasicBlending {
    VkBlendOp blend;
    VkBlendFactor srcBlend;
    VkBlendFactor dstBlend;
};

struct RequiredDescriptors {
    int requireUniformBuffers = 0;
    int requireSamplers = 0;
};

struct PipelineParameters {
    std::vector<std::pair<VkShaderStageFlagBits, std::string>> shadersList;
    bool recompileShaders = false;
    VkPrimitiveTopology topology;

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
    VkExtent2D extent;

    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescription;
    std::vector<VkVertexInputBindingDescription> vertexInputDescription;

    // The number of blending options also determines the number of color attachments.
    std::vector<std::optional<BasicBlending>> blending;
    bool useDepthTest = true;
};

class GraphicsPipeline {
  public:
    GraphicsPipeline(VulkanDevice* device, VkRenderPass renderPass, int subpassId, const PipelineParameters& params);
    ~GraphicsPipeline();

    std::vector<std::pair<std::string, std::string>> errorsFromShaderCompilation;
    VkPipeline pipeline;
    VkPipelineLayout layout;

  private:
    VulkanDevice *device;
};

#endif /* end of include guard: PIPELINE_H */
