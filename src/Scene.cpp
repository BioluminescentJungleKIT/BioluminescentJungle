//
// Created by lars on 28.10.23.
//

#include <iostream>
#include "PhysicalDevice.h"
#include "Pipeline.h"
#include "Scene.h"
#include "VulkanHelper.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>
#include <vulkan/vulkan_core.h>
#include <glm/gtc/type_ptr.hpp>
#include <random>

// Definitions of standard names in gltf
#define BASE_COLOR_TEXTURE "baseColorTexture"
#define FIXED_COLOR "COLOR_0"
#define TEXCOORD0 "TEXCOORD_0"

const int MAX_RECURSION = 10;

static bool meshNeedsColor(const tinygltf::Mesh &mesh) {
    return mesh.primitives[0].attributes.contains(FIXED_COLOR);
}

static bool meshNeedsTexcoords(const tinygltf::Mesh &mesh) {
    return mesh.primitives[0].attributes.contains(TEXCOORD0);
}

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
    bindingDescriptorSets.push_back(uboDescriptorSets[descriptorSetsMap[mesh]]);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
                            bindingDescriptorSets.size(), bindingDescriptorSets.data(), 0, nullptr);

    const char *colorAttr = meshNeedsColor(model.meshes[mesh]) ? FIXED_COLOR : TEXCOORD0;
    for (auto primitive: model.meshes[mesh].primitives) {
        if (primitive.attributes.contains(TEXCOORD0)) {
            auto &material = model.materials[primitive.material];
            auto it = material.values.find(BASE_COLOR_TEXTURE);
            if (it != material.values.end()) {
                // We have a texture here
                const auto &textureIdx = it->second.TextureIndex();
                const tinygltf::Texture &gTexture = model.textures[textureIdx];

                if (!textures.count(gTexture.source)) {
                    throw "Texture not found at runtime??";
                }

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout, 2, 1, &textures[gTexture.source].dSet, 0, nullptr);
            }
        }

        std::vector<VkBuffer> vertex_buffers = {
                buffers[model.bufferViews[model.accessors[primitive.attributes["POSITION"]].bufferView].buffer],
                buffers[model.bufferViews[model.accessors[primitive.attributes[colorAttr]].bufferView].buffer],
        };
        std::vector<VkDeviceSize> offsets = {
                model.bufferViews[model.accessors[primitive.attributes["POSITION"]].bufferView].byteOffset,
                model.bufferViews[model.accessors[primitive.attributes[colorAttr]].bufferView].byteOffset,
        };

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

void Scene::drawPointLights(VkCommandBuffer commandBuffer) {
    if (lights.size()) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &buffers[lightsBuffer], &offset);
        vkCmdDraw(commandBuffer, lights.size(), 1, 0, 0);
    }
}

void Scene::setupDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool) {
    uboDescriptorSets = VulkanHelper::createDescriptorSetsFromLayout(
        device, descriptorPool, uboDescriptorSetLayout, meshTransforms.size());

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
        descriptorWrite.dstSet = uboDescriptorSets[descriptorSetsMap[mesh]];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        descriptorWrite.pImageInfo = nullptr; // Optional
        descriptorWrite.pTexelBufferView = nullptr; // Optional

        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
    }

    if (textures.empty()) {
        return;
    }

    auto textureDescriptorSets = VulkanHelper::createDescriptorSetsFromLayout(device, descriptorPool,
        textureDescriptorSetLayout, textures.size());

    i = 0;
    for (auto &[texId, tex]: textures) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = tex.imageView;
        imageInfo.sampler = tex.sampler;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = textureDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

        tex.dSet = textureDescriptorSets[i];
        ++i;
    }
}

RequiredDescriptors Scene::getNumDescriptors() {
    return RequiredDescriptors {
        .requireUniformBuffers = (int)meshTransforms.size(),
        .requireSamplers = (int)textures.size(),
    };
}

void Scene::setupBuffers(VulkanDevice *device) {
    unsigned long numBuffers = model.buffers.size();
    buffers.resize(model.buffers.size());
    bufferMemories.resize(model.buffers.size());
    for (unsigned long i = 0; i < numBuffers; ++i) {
        auto gltfBuffer = model.buffers[i];
        VkDeviceSize bufferSize = sizeof(gltfBuffer.data[0]) * gltfBuffer.data.size();
        VulkanHelper::createBuffer(*device, device->physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffers[i], bufferMemories[i]);
        VulkanHelper::uploadBuffer(*device, device->physicalDevice, bufferSize, buffers[i],
                                   gltfBuffer.data.data(), device->commandPool, device->graphicsQueue);
    }
    for (auto node: model.scenes[model.defaultScene].nodes) {
        generateTransforms(node, glm::mat4(1.f), MAX_RECURSION);
    }

    if (addArtificialLight && lights.empty()) {
        float fov;
        glm::vec3 lookAt, camera;
        computeCameraPos(lookAt, camera, fov);

        float range = (camera - lookAt).length() * 0.1;

        std::mt19937 mt(0);
        auto distX = std::uniform_real_distribution<float>(-range, range);
        auto distY = std::uniform_real_distribution<float>(-range, range);
        auto distZ = std::uniform_real_distribution<float>(-range * 0.1, range * 0.1);

        for (int i = 0; i < 5; i++) {
            glm::vec3 delta{distX(mt), distY(mt), distZ(mt)};
            lights.push_back({lookAt + delta, glm::vec3((i % 3) / 2.0, (i % 2) / 2.0, (i % 5) / 4.0), 5.0});
        }
    }

    setupUniformBuffers(device);
}

bool Scene::addArtificialLight = false;

void Scene::generateTransforms(int nodeIndex, glm::mat4 oldTransform, int maxRecursion) {
    if (maxRecursion <= 0) return;

    auto node = model.nodes[nodeIndex];
    auto transform = VulkanHelper::transformFromMatrixOrComponents(node.matrix,
                                                                   node.scale, node.rotation, node.translation);
    auto newTransform = oldTransform * transform;

    if (node.mesh >= 0) {
        meshTransforms[node.mesh].push_back(ModelTransform{newTransform});
    } else if (node.extensions.contains("KHR_lights_punctual")) {
        auto light_idx = node.extensions["KHR_lights_punctual"].Get("light").Get<int>();
        auto light = model.extensions["KHR_lights_punctual"].Get("lights").Get(light_idx);
        auto type = light.Get("type").Get<std::string>();
        if (type == "point") {  // we currently do not support "directional" and "spot" lights.
            glm::vec3 light_color = glm::vec3(1.f, 1.f, 1.f);
            if (light.Has("color")) {
                light_color = glm::vec3(light.Get("color").Get(0).Get<double>(),
                                        light.Get("color").Get(1).Get<double>(),
                                        light.Get("color").Get(2).Get<double>());
            }
            float light_intensity = 1;
            if (light.Has("intensity")) {
                light_intensity = static_cast<float>(light.Get("intensity").Get<double>());
            }
            lights.push_back({glm::make_vec3(newTransform[3]), light_color, light_intensity});
        } else {
            std::cout << "[lights] WARN: Detected unsupported light of type " << type << std::endl;
        }
    }

    for (int child: node.children) {
        generateTransforms(child, newTransform, maxRecursion - 1);
    }
}

void Scene::setupUniformBuffers(VulkanDevice *device) {
    for (auto [mesh, transforms]: meshTransforms) {
        VkBuffer buffer;
        VkDeviceMemory bufferMemory;
        VkDeviceSize bufferSize = sizeof(ModelTransform) * transforms.size();
        VulkanHelper::createBuffer(*device, device->physicalDevice, bufferSize,
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, bufferMemory);

        VulkanHelper::uploadBuffer(*device, device->physicalDevice, bufferSize, buffer, transforms.data(),
                                   device->commandPool, device->graphicsQueue);

        buffersMap[mesh] = buffers.size();
        buffers.push_back(buffer);
        bufferMemories.push_back(bufferMemory);
    }
    if (lights.size() > 0) {
        VkBuffer buffer;
        VkDeviceMemory bufferMemory;
        VkDeviceSize bufferSize = sizeof(LightData) * lights.size();
        VulkanHelper::createBuffer(*device, device->physicalDevice, bufferSize,
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, bufferMemory);

        VulkanHelper::uploadBuffer(*device, device->physicalDevice, bufferSize, buffer, lights.data(),
                                   device->commandPool, device->graphicsQueue);

        lightsBuffer = buffers.size();
        buffers.push_back(buffer);
        bufferMemories.push_back(bufferMemory);
    }
}

void Scene::destroyBuffers(VkDevice device) {
    for (auto buffer: buffers) vkDestroyBuffer(device, buffer, nullptr);
    for (auto bufferMemory: bufferMemories) vkFreeMemory(device, bufferMemory, nullptr);
}

std::tuple<std::vector<VkVertexInputAttributeDescription>, std::vector<VkVertexInputBindingDescription>>
Scene::getLightsAttributeAndBindingDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;

    VkVertexInputBindingDescription bindingDescription;
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(LightData);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindingDescriptions.push_back(bindingDescription);

    VkVertexInputAttributeDescription positionAttributeDescription{};
    positionAttributeDescription.binding = 0;
    positionAttributeDescription.location = 0;
    positionAttributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    positionAttributeDescription.offset = offsetof(LightData, position);
    attributeDescriptions.push_back(positionAttributeDescription);

    VkVertexInputAttributeDescription colorAttributeDescription{};
    colorAttributeDescription.binding = 0;
    colorAttributeDescription.location = 1;
    colorAttributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    colorAttributeDescription.offset = offsetof(LightData, color);
    attributeDescriptions.push_back(colorAttributeDescription);

    VkVertexInputAttributeDescription intensityAttributeDescription{};
    intensityAttributeDescription.binding = 0;
    intensityAttributeDescription.location = 2;
    intensityAttributeDescription.format = VK_FORMAT_R32_SFLOAT;
    intensityAttributeDescription.offset = offsetof(LightData, intensity);
    attributeDescriptions.push_back(intensityAttributeDescription);

    return {attributeDescriptions, bindingDescriptions};
}

std::tuple<std::vector<VkVertexInputAttributeDescription>, std::vector<VkVertexInputBindingDescription>>
Scene::getAttributeAndBindingDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;

    // TODO make this more dynamic: allow either for all meshes, use default values in case the attribute is not present
    if (meshNeedsColor(model.meshes[0])) {
        int colorAccessor = model.meshes[0].primitives[0].attributes[FIXED_COLOR];
        VkVertexInputAttributeDescription colorAttributeDescription{};
        colorAttributeDescription.binding = 1; // TODO can this really be hardcoded, when getVertexBindingDescription could in theory return a different binding?
        colorAttributeDescription.location = 1;
        colorAttributeDescription.format =
                VulkanHelper::gltfTypeToVkFormat(model.accessors[colorAccessor].type,
                                                 model.accessors[colorAccessor].componentType,
                                                 model.accessors[colorAccessor].normalized);
        colorAttributeDescription.offset = 0;
        attributeDescriptions.push_back(colorAttributeDescription);
        bindingDescriptions.push_back(getVertexBindingDescription(colorAccessor, 1));
    } else if (meshNeedsTexcoords(model.meshes[0])) {
        int texcoordAccessor = model.meshes[0].primitives[0].attributes[TEXCOORD0];
        VkVertexInputAttributeDescription texcoordAttributeDescription{};
        texcoordAttributeDescription.binding = 1;
        texcoordAttributeDescription.location = 1;
        texcoordAttributeDescription.format =
                VulkanHelper::gltfTypeToVkFormat(model.accessors[texcoordAccessor].type,
                                                 model.accessors[texcoordAccessor].componentType,
                                                 model.accessors[texcoordAccessor].normalized);
        texcoordAttributeDescription.offset = 0;
        attributeDescriptions.push_back(texcoordAttributeDescription);
        bindingDescriptions.push_back(getVertexBindingDescription(texcoordAccessor, 1));
    }

    int positionAccessor = model.meshes[0].primitives[0].attributes["POSITION"];
    VkVertexInputAttributeDescription positionAttributeDescription{};
    positionAttributeDescription.binding = 0;
    positionAttributeDescription.location = 0;
    positionAttributeDescription.format = VulkanHelper::gltfTypeToVkFormat(model.accessors[positionAccessor].type,
                                                                           model.accessors[positionAccessor].componentType,
                                                                           model.accessors[positionAccessor].normalized);
    positionAttributeDescription.offset = 0;
    attributeDescriptions.push_back(positionAttributeDescription);
    bindingDescriptions.push_back(getVertexBindingDescription(positionAccessor, 0));

    return {attributeDescriptions, bindingDescriptions};
}

VkVertexInputBindingDescription Scene::getVertexBindingDescription(int accessor, int bindingId) {
    if (vertexBindingDescriptions.contains(accessor)) {
        return vertexBindingDescriptions[accessor];
    }

    VkVertexInputBindingDescription bindingDescription;

    bindingDescription.binding = bindingId;
    bindingDescription.stride = VulkanHelper::strideFromGltfType(
            model.accessors[accessor].type,
            model.accessors[accessor].componentType,
            model.bufferViews[model.accessors[accessor].bufferView].byteStride);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    vertexBindingDescriptions[accessor] = bindingDescription;

    return bindingDescription;
}

std::vector<VkDescriptorSetLayout> Scene::getDescriptorSetLayouts(VkDevice device) {

    if (uboDescriptorSetLayout == VK_NULL_HANDLE) {
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

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &uboDescriptorSetLayout))
    }

    if (textureDescriptorSetLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 0;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &samplerLayoutBinding;

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &textureDescriptorSetLayout))
    }

    return {uboDescriptorSetLayout, textureDescriptorSetLayout};
}

void Scene::destroyDescriptorSetLayout(VkDevice device) {
    vkDestroyDescriptorSetLayout(device, uboDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, textureDescriptorSetLayout, nullptr);
}

void calculateBoundingBox(const tinygltf::Model &model, glm::vec3 &minBounds, glm::vec3 &maxBounds) {
    minBounds = glm::vec3(std::numeric_limits<float>::max());
    maxBounds = glm::vec3(-std::numeric_limits<float>::max());

    for (const auto &mesh: model.meshes) {
        for (const auto &primitive: mesh.primitives) {
            const auto &attributes = primitive.attributes;
            if (attributes.find("POSITION") != attributes.end()) {
                const int accessorIdx = attributes.at("POSITION");
                const auto &accessor = model.accessors[accessorIdx];
                const auto &bufferView = model.bufferViews[accessor.bufferView];
                const auto &buffer = model.buffers[bufferView.buffer];
                const size_t byteStride = accessor.ByteStride(bufferView);

                for (size_t i = 0; i < accessor.count; ++i) {
                    const int pos = bufferView.byteOffset + accessor.byteOffset + i * byteStride;
                    const float *ptr = reinterpret_cast<const float *>(&buffer.data[pos]);
                    for (int j = 0; j < 3; ++j) {
                        minBounds[j] = std::min(minBounds[j], ptr[j]);
                        maxBounds[j] = std::max(maxBounds[j], ptr[j]);
                    }
                }
            }
        }
    }
}

std::ostream &operator<<(std::ostream &out, const glm::vec3 &value) {
    out << std::setprecision(4) << "(" << value.x << "," << value.y << "," << value.z << ")";
    return out;
}

void Scene::computeCameraPos(glm::vec3 &lookAt, glm::vec3 &cameraPos, float &fov) {
    // TODO: if the scene has a camera, we ought to load the data from it

    // Compute bbox of the meshes
    glm::vec3 min, max;
    calculateBoundingBox(model, min, max);

    fov = 45.0f;
    lookAt = (min + max) / 2.0f;
    float R = glm::length(max - min);
    R /= std::tan(fov * M_PI / 360);
    R *= 0.6;
    cameraPos = glm::vec3(lookAt.x, lookAt.y + R, lookAt.z + R);
}

void copyBufferToImage(VulkanDevice *device, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    device->endSingleTimeCommands(commandBuffer);
}

Scene::LoadedTexture uploadGLTFImage(VulkanDevice *device, const tinygltf::Image &image) {
    Scene::LoadedTexture loadedTex;
    const uint32_t imageSize = image.width * image.height * image.component;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VulkanHelper::createBuffer(device->device, device->physicalDevice,
                               imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               stagingBuffer, stagingBufferMemory);

    void *data;
    vkMapMemory(*device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, image.image.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(*device, stagingBufferMemory);

    device->createImage(image.width, image.height, VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, loadedTex.image, loadedTex.memory);

    device->transitionImageLayout(loadedTex.image, VK_FORMAT_R8G8B8A8_SRGB,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(device, stagingBuffer, loadedTex.image, image.width, image.height);
    device->transitionImageLayout(loadedTex.image, VK_FORMAT_R8G8B8A8_SRGB,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(*device, stagingBuffer, nullptr);
    vkFreeMemory(*device, stagingBufferMemory, nullptr);
    return loadedTex;
}

void Scene::setupTextures(VulkanDevice *device) {
    for (size_t i = 0; i < model.materials.size(); i++) {
        const auto &material = model.materials[i];

        auto it = material.values.find(BASE_COLOR_TEXTURE);
        if (it == material.values.end()) {
            continue;
        }

        // We have a texture here
        const auto &textureIdx = it->second.TextureIndex();
        const tinygltf::Texture &gTexture = model.textures[textureIdx];

        if (textures.count(gTexture.source)) {
            continue;
        }

        textures[gTexture.source] = uploadGLTFImage(device, model.images[gTexture.source]);
        textures[gTexture.source].imageView = device->createImageView(textures[gTexture.source].image,
                                                                      VK_FORMAT_R8G8B8A8_SRGB,
                                                                      VK_IMAGE_ASPECT_COLOR_BIT);
        textures[gTexture.source].sampler = VulkanHelper::createSampler(device);
    }
}

void Scene::destroyTextures(VulkanDevice *device) {
    for (auto &[idx, tex]: textures) {
        vkDestroyImageView(*device, tex.imageView, nullptr);
        vkDestroyImage(*device, tex.image, nullptr);
        vkDestroySampler(*device, tex.sampler, nullptr);
        vkFreeMemory(*device, tex.memory, nullptr);
    }
}

std::string Scene::queryShaderName() {
    return meshNeedsColor(model.meshes[0]) ? "shaders/shader" : "shaders/simple-texture";
}
