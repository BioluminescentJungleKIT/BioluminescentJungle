//
// Created by lars on 28.10.23.
//

#include <iostream>
#include "Scene.h"
#include "VulkanHelper.h"

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

void Scene::render(VkCommandBuffer commandBuffer) {
    renderNode(model.defaultScene, commandBuffer, 32);
}

void Scene::renderNode(int node, VkCommandBuffer commandBuffer, int maxRecursion) {
    if (maxRecursion <= 0) return;
    if (model.nodes[node].mesh >= 0) {
        auto mesh = model.meshes[model.nodes[node].mesh];
        for (auto primitive: mesh.primitives) {
            std::vector<VkBuffer> vertex_buffers = {buffers[model.bufferViews[model.accessors[primitive.attributes["COLOR_0"]].bufferView].buffer],
                                                    buffers[model.bufferViews[model.accessors[primitive.attributes["POSITION"]].bufferView].buffer]};
            std::vector<VkDeviceSize> offsets = {model.bufferViews[model.accessors[primitive.attributes["COLOR_0"]].bufferView].byteOffset, model.bufferViews[model.accessors[primitive.attributes["POSITION"]].bufferView].byteOffset};

            // todo upload and bind transformations

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

                vkCmdDrawIndexed(commandBuffer, numIndices, 1, 0, 0, 0);
            } else {
                throw std::runtime_error("Non-indexed geometry is currently not supported.");
            }
        }
    }
    for (int child: model.nodes[node].children) {
        renderNode(child, commandBuffer, maxRecursion - 1);
    }
}

void Scene::setupDescriptorSets() {

}

uint32_t Scene::getDescriptorCount() {
    return 1;
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
}

void Scene::destroyBuffers(VkDevice device) {
    for (auto buffer: buffers) vkDestroyBuffer(device, buffer, nullptr);
    for (auto bufferMemory: bufferMemories) vkFreeMemory(device, bufferMemory, nullptr);
}

std::tuple<std::vector<VkVertexInputAttributeDescription>, std::vector<VkVertexInputBindingDescription>> Scene::getAttributeAndBindingDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;

    // TODO default color if not set
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

