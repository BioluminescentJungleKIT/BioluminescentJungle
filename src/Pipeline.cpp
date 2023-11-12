#include "Pipeline.h"
#include "VulkanHelper.h"
#include <shaderc/shaderc.h>
#include <vulkan/vulkan_core.h>

static shaderc_shader_kind getShadercType(VkShaderStageFlagBits stageType) {
    switch (stageType) {
        case VK_SHADER_STAGE_VERTEX_BIT:
          return shaderc_vertex_shader;
        case VK_SHADER_STAGE_GEOMETRY_BIT:
          return shaderc_geometry_shader;
        case VK_SHADER_STAGE_FRAGMENT_BIT:
          return shaderc_fragment_shader;

        default:
          // TODO: we currently do not need all stages
          throw std::runtime_error("Unsupported shader type!");
          /*
             case VK_SHADER_STAGE_COMPUTE_BIT:
             case VK_SHADER_STAGE_ALL_GRAPHICS:
             case VK_SHADER_STAGE_ALL:
             case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
             case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
             case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
             case VK_SHADER_STAGE_MISS_BIT_KHR:
             case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
             case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
             case VK_SHADER_STAGE_TASK_BIT_NV:
             case VK_SHADER_STAGE_MESH_BIT_NV:
             case VK_SHADER_STAGE_SUBPASS_SHADING_BIT_HUAWEI:
             case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
             case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
             */
    }
}

static VkShaderModule createShaderModule(VulkanDevice *device, std::vector<char> code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
    VkShaderModule shaderModule;
    VK_CHECK_RESULT(vkCreateShaderModule(*device, &createInfo, nullptr, &shaderModule))
    return shaderModule;
}

GraphicsPipeline::GraphicsPipeline(VulkanDevice* device, VkRenderPass renderPass, int subpassId,
    const PipelineParameters& params) {

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::vector<VkShaderModule> shaderModules;

    for (auto& [type, shaderFile] : params.shadersList) {
        auto [code, message] = getShaderCode(shaderFile, getShadercType(type), params.recompileShaders);
        if (!message.empty()) {
            this->errorsFromShaderCompilation.emplace_back(shaderFile, message);
            std::cout << "Error while compiling shader " << shaderFile << ":" << std::endl;
            std::cout << message << std::endl;
        }

        VkShaderModule module = createShaderModule(device, code);
        shaderModules.push_back(module);

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = type;
        shaderStageInfo.module = module;
        shaderStageInfo.pName = "main";
        stages.push_back(shaderStageInfo);
    }

    std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(params.vertexInputDescription.size());
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(params.vertexAttributeDescription.size());
    vertexInputInfo.pVertexBindingDescriptions = params.vertexInputDescription.data();
    vertexInputInfo.pVertexAttributeDescriptions = params.vertexAttributeDescription.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = params.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) params.extent.width;
    viewport.height = (float) params.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = params.extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f; // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f; // Optional
    multisampling.pSampleMask = nullptr; // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional


    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;

    for (auto& blending : params.blending) {
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = 0xf;
        colorBlendAttachment.blendEnable = blending.has_value() ? VK_TRUE : VK_FALSE;
        if (blending) {
            colorBlendAttachment.srcColorBlendFactor = blending->srcBlend;
            colorBlendAttachment.dstColorBlendFactor = blending->dstBlend;
            colorBlendAttachment.colorBlendOp = blending->blend;
            colorBlendAttachment.srcAlphaBlendFactor = blending->srcBlend;
            colorBlendAttachment.dstAlphaBlendFactor = blending->dstBlend;
            colorBlendAttachment.alphaBlendOp = blending->blend;
        }

        colorBlendAttachments.push_back(colorBlendAttachment);
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = colorBlendAttachments.size();
    colorBlending.pAttachments = colorBlendAttachments.data();

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = params.useDepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = params.useDepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = params.descriptorSetLayouts.size();
    pipelineLayoutInfo.pSetLayouts = params.descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
    pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional
    VK_CHECK_RESULT(vkCreatePipelineLayout(*device, &pipelineLayoutInfo, nullptr, &layout))

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = stages.size();
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpassId;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1; // Optional

    VK_CHECK_RESULT(vkCreateGraphicsPipelines(*device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));
    for (auto& module : shaderModules) {
        vkDestroyShaderModule(*device, module, nullptr);
    }

    this->device = device;
}

std::vector<std::pair<std::string, std::string>> GraphicsPipeline::errorsFromShaderCompilation;

GraphicsPipeline::~GraphicsPipeline() {
    vkDestroyPipeline(*device, pipeline, nullptr);
    vkDestroyPipelineLayout(*device, layout, nullptr);
}
