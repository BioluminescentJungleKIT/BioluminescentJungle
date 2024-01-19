#ifndef PIPELINE_H
#define PIPELINE_H

#include "PhysicalDevice.h"
#include <string>
#include <vulkan/vulkan_core.h>

/**
 * A class which makes it easier to manage graphicsPipelines.
 */

struct BasicBlending {
    VkBlendOp blend;
    VkBlendFactor srcBlend;
    VkBlendFactor dstBlend;
};

struct RequiredDescriptors {
    unsigned int requireUniformBuffers = 0;
    unsigned int requireSamplers = 0;
    unsigned int requireSSBOs = 0;
};

using ShaderSource = std::pair<VkShaderStageFlagBits, std::string>;
using ShaderList = std::vector<ShaderSource>;

struct PipelineParameters {
    ShaderList shadersList;
    bool recompileShaders = false;
    VkPrimitiveTopology topology;

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
    VkExtent2D extent;

    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescription;
    std::vector<VkVertexInputBindingDescription> vertexInputDescription;

    // The number of blending options also determines the number of color attachments.
    std::vector<std::optional<BasicBlending>> blending;
    bool useDepthTest = true;
    std::vector<VkPushConstantRange> pushConstants;
    bool backFaceCulling = true;
    bool isButterfly;
};

class GraphicsPipeline {
  public:
    GraphicsPipeline(VulkanDevice* device, VkRenderPass renderPass, int subpassId, const PipelineParameters& params);
    ~GraphicsPipeline();

    static std::vector<std::pair<std::string, std::string>> errorsFromShaderCompilation;

    VkPipeline pipeline;
    VkPipelineLayout layout;

    bool isButterfly;

  private:
    VulkanDevice *device;
};

class ComputePipeline {
  public:
    struct Parameters {
        ShaderSource source;
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
        std::vector<VkPushConstantRange> pushConstantRanges;
        bool recompileShaders = false;
    };

    ComputePipeline(VulkanDevice* device, const Parameters& params);
    ~ComputePipeline();

    VkPipeline pipeline;
    VkPipelineLayout layout;

  private:
    VulkanDevice *device;
};

#endif /* end of include guard: PIPELINE_H */
