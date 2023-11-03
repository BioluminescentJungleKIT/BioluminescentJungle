//
// Created by lars on 28.10.23.
//

#include <iostream>
#include "Scene.h"
#include "VulkanHelper.h"
#include <glm/gtc/matrix_transform.hpp>

const int MAX_RECURSION = 10;

Scene::Scene(std::string filename) {
    std::string err, warn;
    loader.LoadASCIIFromFile(&model, &err, &warn, filename);
    if (!warn.empty()) {
        std::cout << "[loader] WARN: " << warn << std::endl;
    }

    if (!err.empty()) {
        throw std::runtime_error("[loader] ERR: " + err);
    }
}

void
Scene::render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout, VkDescriptorSet globalDescriptorSet) {
    for (int mesh = 0; mesh < model.meshes.size(); ++mesh) {
        renderInstances(mesh, commandBuffer, pipelineLayout, globalDescriptorSet);
    }
}


void Scene::renderInstances(int mesh, VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout,
                            VkDescriptorSet globalDescriptorSet) {
    if (meshTransforms.find(mesh) == meshTransforms.end()) return; // 0 instances. skip.

    // bind transformations
    bindingDescriptorSets.clear();
    bindingDescriptorSets.push_back(globalDescriptorSet);
    bindingDescriptorSets.push_back(descriptorSets[descriptorSetsMap[mesh]]);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
                            bindingDescriptorSets.size(), bindingDescriptorSets.data(), 0, nullptr);

    for (auto primitive: model.meshes[mesh].primitives) {

        std::vector<VkBuffer> vertex_buffers = {
                buffers[model.bufferViews[model.accessors[primitive.attributes["COLOR_0"]].bufferView].buffer],
                buffers[model.bufferViews[model.accessors[primitive.attributes["POSITION"]].bufferView].buffer]};
        std::vector<VkDeviceSize> offsets = {
                model.bufferViews[model.accessors[primitive.attributes["COLOR_0"]].bufferView].byteOffset,
                model.bufferViews[model.accessors[primitive.attributes["POSITION"]].bufferView].byteOffset};


        vkCmdBindVertexBuffers(commandBuffer, 0, vertex_buffers.size(), vertex_buffers.data(), offsets.data());

        if (primitive.indices >= 0) {
            auto indexAccessorIndex = primitive.indices;
            auto indexBufferViewIndex = model.accessors[indexAccessorIndex].bufferView;
            auto indexBufferIndex = model.bufferViews[indexBufferViewIndex].buffer;
            auto indexBuffer = buffers[indexBufferIndex];
            auto indexBufferOffset = model.bufferViews[indexBufferViewIndex].byteOffset;
            uint32_t numIndices = model.accessors[indexAccessorIndex].count;
            auto indexBufferType = VulkanHelper::gltfTypeToVkIndexType(
                    model.accessors[indexAccessorIndex].componentType);
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer, indexBufferOffset, indexBufferType);

            vkCmdDrawIndexed(commandBuffer, numIndices, meshTransforms[mesh].size(), 0, 0, 0);
        } else {
            throw std::runtime_error("Non-indexed geometry is currently not supported.");
        }
    }

}

void Scene::setupDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                                VkDescriptorSetLayout descriptorSetLayout) {
    std::vector<VkDescriptorSetLayout> layouts(meshTransforms.size(), sceneDescriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = meshTransforms.size();
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(meshTransforms.size());

    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()))

    int i = 0;
    for (int mesh = 0; mesh < model.meshes.size(); mesh++) {
        if (meshTransforms.find(mesh) == meshTransforms.end()) continue; // 0 instances. skip.

        auto meshTransformsBufferIndex = buffersMap[mesh];
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffers[meshTransformsBufferIndex];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ModelTransform) * meshTransforms[mesh].size();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorSetsMap[mesh] = i++;
        descriptorWrite.dstSet = descriptorSets[descriptorSetsMap[mesh]];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        descriptorWrite.pImageInfo = nullptr; // Optional
        descriptorWrite.pTexelBufferView = nullptr; // Optional

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }
}

uint32_t Scene::getNumDescriptorSets() {
    return meshTransforms.size();
}

void Scene::setupBuffers(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue) {
    unsigned long numBuffers = model.buffers.size();
    buffers.resize(model.buffers.size());
    bufferMemories.resize(model.buffers.size());
    for (unsigned long i = 0; i < numBuffers; ++i) {
        auto gltfBuffer = model.buffers[i];
        VkDeviceSize bufferSize = sizeof(gltfBuffer.data[0]) * gltfBuffer.data.size();
        VulkanHelper::createBuffer(device, physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffers[i], bufferMemories[i]);
        VulkanHelper::uploadBuffer(device, physicalDevice, bufferSize, buffers[i], gltfBuffer.data.data(), commandPool,
                                   queue);
    }
    for (auto node: model.scenes[model.defaultScene].nodes) {
        generateTransforms(node, glm::mat4(1.f), MAX_RECURSION);
        setupUniformBuffers(device, physicalDevice, commandPool, queue);
    }
}

void Scene::generateTransforms(int nodeIndex, glm::mat4 oldTransform, int maxRecursion) {
    if (maxRecursion <= 0) return;

    auto node = model.nodes[nodeIndex];
    auto transform = VulkanHelper::transformFromMatrixOrComponents(node.matrix,
                                                                   node.scale, node.rotation, node.translation);
    auto newTransform = oldTransform * transform;

    if (node.mesh >= 0) {
        meshTransforms[node.mesh].push_back(ModelTransform{newTransform});
    }

    for (int child: node.children) {
        generateTransforms(child, newTransform, maxRecursion - 1);
    }
}

void Scene::setupUniformBuffers(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool,
                                VkQueue queue) {
    for (auto [mesh, transforms]: meshTransforms) {
        VkBuffer buffer;
        VkDeviceMemory bufferMemory;
        VkDeviceSize bufferSize = sizeof(ModelTransform) * transforms.size();
        VulkanHelper::createBuffer(device, physicalDevice, bufferSize,
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, bufferMemory);

        VulkanHelper::uploadBuffer(device, physicalDevice, bufferSize, buffer, transforms.data(), commandPool, queue);

        buffersMap[mesh] = buffers.size();
        buffers.push_back(buffer);
        bufferMemories.push_back(bufferMemory);
    }
}

void Scene::destroyBuffers(VkDevice device) {
    for (auto buffer: buffers) vkDestroyBuffer(device, buffer, nullptr);
    for (auto bufferMemory: bufferMemories) vkFreeMemory(device, bufferMemory, nullptr);
}

std::tuple<std::vector<VkVertexInputAttributeDescription>, std::vector<VkVertexInputBindingDescription>>
Scene::getAttributeAndBindingDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;

    // TODO set default color (white) in case the attribute is not present
    int colorAccessor = model.meshes[0].primitives[0].attributes["COLOR_0"];
    VkVertexInputAttributeDescription colorAttributeDescription{};
    colorAttributeDescription.binding = colorAccessor;
    colorAttributeDescription.location = 1;
    colorAttributeDescription.format = VulkanHelper::gltfTypeToVkFormat(model.accessors[colorAccessor].type,
                                                                        model.accessors[colorAccessor].componentType,
                                                                        model.accessors[colorAccessor].normalized);
    colorAttributeDescription.offset = 0;
    attributeDescriptions.push_back(colorAttributeDescription);
    bindingDescriptions.push_back(getVertexBindingDescription(colorAccessor));


    int positionAccessor = model.meshes[0].primitives[0].attributes["POSITION"];
    VkVertexInputAttributeDescription positionAttributeDescription{};
    positionAttributeDescription.binding = positionAccessor;
    positionAttributeDescription.location = 0;
    positionAttributeDescription.format = VulkanHelper::gltfTypeToVkFormat(model.accessors[positionAccessor].type,
                                                                           model.accessors[positionAccessor].componentType,
                                                                           model.accessors[positionAccessor].normalized);
    positionAttributeDescription.offset = 0;
    attributeDescriptions.push_back(positionAttributeDescription);
    bindingDescriptions.push_back(getVertexBindingDescription(positionAccessor));

    return {attributeDescriptions, bindingDescriptions};
}

VkVertexInputBindingDescription Scene::getVertexBindingDescription(int accessor) {
    if (vertexBindingDescriptions.contains(accessor)) {
        return vertexBindingDescriptions[accessor];
    }

    VkVertexInputBindingDescription bindingDescription;

    bindingDescription.binding = accessor;
    bindingDescription.stride = VulkanHelper::strideFromGltfType(
            model.accessors[accessor].type,
            model.accessors[accessor].componentType,
            model.bufferViews[model.accessors[accessor].bufferView].byteStride);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    vertexBindingDescriptions[accessor] = bindingDescription;

    return bindingDescription;
}

VkDescriptorSetLayout Scene::getDescriptorSetLayout(VkDevice device) {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &sceneDescriptorSetLayout))

    return sceneDescriptorSetLayout;
}

void Scene::destroyDescriptorSetLayout(VkDevice device) {
    vkDestroyDescriptorSetLayout(device, sceneDescriptorSetLayout, nullptr);
}

