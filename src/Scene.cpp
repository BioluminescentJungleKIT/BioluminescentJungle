//
// Created by lars on 28.10.23.
//

#include <iostream>
#include "PhysicalDevice.h"
#include "GBufferDescription.h"
#include "Pipeline.h"
#include "Scene.h"
#include "VulkanHelper.h"
#include "imgui.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>
#include <stdexcept>
#include <vulkan/vulkan_core.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <memory>

// Definitions of standard names in gltf
#define POSITION "POSITION"
#define BASE_COLOR_TEXTURE "baseColorTexture"
#define FIXED_COLOR "COLOR_0"
#define TEXCOORD0 "TEXCOORD_0"
#define NORMAL "NORMAL"

const int MAX_RECURSION = 10;

static bool string_contains(std::string a, std::string b) {
    return a.find(b) != a.npos;
}

static bool materialUsesDisplacedTexture(const tinygltf::Material& material) {
    return material.occlusionTexture.index >= 0;
}

static bool materialUsesNormalTexture(const tinygltf::Material& material) {
    return material.normalTexture.index >= 0;
}

static bool materialUsesSSR(const tinygltf::Material& material) {
    return material.name.find("SSR") != material.name.npos;
}

static bool materialUsesBaseTexture(const tinygltf::Material& material) {
    return material.values.count(BASE_COLOR_TEXTURE);
}

PipelineDescription Scene::getPipelineDescriptionForPrimitive(const tinygltf::Primitive &primitive) {
    PipelineDescription descr;

    const auto &attributes = primitive.attributes;

    if (attributes.count(POSITION)) {
        descr.vertexPosAccessor = attributes.at(POSITION);
    }

    if (attributes.count(NORMAL)) {
        descr.vertexNormalAccessor = attributes.at(NORMAL);
    }

    bool has_texcoords = attributes.count(TEXCOORD0);
    auto& material = model.materials[primitive.material];

    if (has_texcoords && materialUsesBaseTexture(material)) {
        descr.vertexTexcoordsAccessor = attributes.at(TEXCOORD0);

        if (materialUsesSSR(material)) {
            descr.useSSR = true;
        }
    } else if (attributes.count(FIXED_COLOR)) {
        descr.vertexFixedColorAccessor = attributes.at(FIXED_COLOR);
    }

    if (has_texcoords && materialUsesNormalTexture(material)) {
        descr.useNormalMap = true;
        if (materialUsesDisplacedTexture(material)) {
            descr.useDisplacement = true;
        }
    }

    return descr;
}

Scene::Scene(VulkanDevice *device, Swapchain *swapchain, std::string filename) {
    this->device = device;
    this->swapchain = swapchain;

    std::string err, warn;
    loader.LoadASCIIFromFile(&model, &err, &warn, filename);
    if (!warn.empty()) {
        std::cout << "[loader] WARN: " << warn << std::endl;
    }

    if (!err.empty()) {
        throw std::runtime_error("[loader] ERR: " + err);
    }

    // We precompute a list of mesh primitives to be rendered with each of the generated programs.
    for (size_t i = 0; i < model.meshes.size(); i++) {
        for (size_t j = 0; j < model.meshes[i].primitives.size(); j++) {
            if (model.meshes[i].primitives[j].material < 0) {
                std::cout << "Unsupported primitive meshId=" << i << " primitiveId=" << j
                          << ": no material specified." << std::endl;
                continue;
            }

            auto descr = getPipelineDescriptionForPrimitive(model.meshes[i].primitives[j]);

            if (!descr.vertexTexcoordsAccessor.has_value() && !descr.vertexFixedColorAccessor.has_value()) {
                std::cout << "Unsupported primitive meshId=" << i << " primitiveId=" << j
                          << ": no texture or vertex color specified." << std::endl;
                continue;
            }

            meshPrimitivesWithPipeline[descr][i].emplace_back(j);
        }
    }
}

void Scene::recordCommandBuffer(VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet) {
    // Draw all primitives with all available pipelines.
    for (auto &[programDescr, meshMap]: meshPrimitivesWithPipeline) {
        auto &pipeline = pipelines[programDescr];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
        VulkanHelper::setFullViewportScissor(commandBuffer, swapchain->renderSize());

        for (auto &[meshId, primitivesList]: meshMap) {
            // bind transformations for each mesh
            bindingDescriptorSets.clear();
            bindingDescriptorSets.push_back(mvpSet);
            bindingDescriptorSets.push_back(uboDescriptorSets[descriptorSetsMap[meshId]]);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, 0,
                                    bindingDescriptorSets.size(), bindingDescriptorSets.data(), 0, nullptr);

            for (auto &primitiveId: primitivesList) {
                // Bind the specific texture for each primitive
                renderPrimitiveInstances(meshId, primitiveId, commandBuffer, programDescr, pipeline->layout);
            }
        }
    }
}

void Scene::renderPrimitiveInstances(int meshId, int primitiveId, VkCommandBuffer commandBuffer,
                                     const PipelineDescription &descr, VkPipelineLayout pipelineLayout) {
    if (meshTransforms.find(meshId) == meshTransforms.end()) return; // 0 instances. skip.
    //
    auto &primitive = model.meshes[meshId].primitives[primitiveId];
    if (materialDSet.contains(primitive.material)) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 2, 1, &materialDSet[primitive.material], 0, nullptr);
    }

    if (descr.useNormalMap) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 3, 1, &materialSettingSets[swapchain->currentFrame], 0, nullptr);
    }

    std::vector<VkBuffer> vertexBuffers;
    std::vector<VkDeviceSize> offsets;
    const auto &addBufferOffset = [&](const char *attribute) {
        const auto &bufferView = model.bufferViews[model.accessors[primitive.attributes[attribute]].bufferView];
        vertexBuffers.push_back(buffers[bufferView.buffer].buffer);
        offsets.push_back(bufferView.byteOffset);
    };

    if (descr.vertexPosAccessor.has_value()) {
        addBufferOffset(POSITION);
    }
    if (descr.vertexFixedColorAccessor.has_value()) {
        addBufferOffset(FIXED_COLOR);
    } else if (descr.vertexTexcoordsAccessor.has_value()) {
        addBufferOffset(TEXCOORD0);
    }
    if (descr.vertexNormalAccessor.has_value()) {
        addBufferOffset(NORMAL);
    }

    vkCmdBindVertexBuffers(commandBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets.data());

    if (primitive.indices >= 0) {
        auto indexAccessorIndex = primitive.indices;
        auto indexBufferViewIndex = model.accessors[indexAccessorIndex].bufferView;
        auto indexBufferIndex = model.bufferViews[indexBufferViewIndex].buffer;
        auto indexBuffer = buffers[indexBufferIndex];
        auto indexBufferOffset = model.bufferViews[indexBufferViewIndex].byteOffset;
        uint32_t numIndices = model.accessors[indexAccessorIndex].count;
        auto indexBufferType = VulkanHelper::gltfTypeToVkIndexType(
                model.accessors[indexAccessorIndex].componentType);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, indexBufferOffset, indexBufferType);
        vkCmdDrawIndexed(commandBuffer, numIndices, meshTransforms[meshId].size(), 0, 0, 0);
    } else {
        throw std::runtime_error("Non-indexed geometry is currently not supported.");
    }
}

void Scene::drawPointLights(VkCommandBuffer commandBuffer) {
    if (lights.size()) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &buffers[lightsBuffer].buffer, &offset);
        vkCmdDraw(commandBuffer, lights.size(), 1, 0, 0);
    }
}

void Scene::setupDescriptorSets(VkDescriptorPool descriptorPool) {
    uboDescriptorSets = VulkanHelper::createDescriptorSetsFromLayout(
            *device, descriptorPool, uboDescriptorSetLayout, meshTransforms.size());

    int i = 0;
    for (int mesh = 0; mesh < model.meshes.size(); mesh++) {
        if (meshTransforms.find(mesh) == meshTransforms.end()) continue; // 0 instances. skip.
        auto meshTransformsBufferIndex = buffersMap[mesh];
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffers[meshTransformsBufferIndex].buffer;
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

        vkUpdateDescriptorSets(*device, 1, &descriptorWrite, 0, nullptr);
    }

    materialSettingSets = VulkanHelper::createDescriptorSetsFromLayout(*device, descriptorPool,
        materialsSettingsLayout, MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        auto bInfo = vkutil::createDescriptorBufferInfo(materialBuffer.buffers[i], 0, sizeof(MaterialSettings));
        device->writeDescriptorSets({
            vkutil::createDescriptorWriteUBO(bInfo, materialSettingSets[i], 0),
        });
    }

    const auto& findTexture = [&] (int gltfId) -> LoadedTexture& {
        auto gTex = model.textures[gltfId];
        return textures[gTex.source];
    };

    for (size_t i = 0; i < model.materials.size(); i++) {
        VkDescriptorSetLayout desiredLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorImageInfo> boundTextures;
        std::vector<VkDescriptorBufferInfo> boundBuffers;
        if (materialUsesBaseTexture(model.materials[i])) {
            desiredLayout = albedoDSLayout;
            auto& albedo = findTexture(model.materials[i].values.find(BASE_COLOR_TEXTURE)->second.TextureIndex());
            boundTextures.push_back(vkutil::createDescriptorImageInfo(albedo.imageView, albedo.sampler));

            if (materialUsesNormalTexture(model.materials[i])) {
                desiredLayout = albedoDisplacementDSLayout;
                auto& normal = findTexture(model.materials[i].normalTexture.index);
                boundTextures.push_back(vkutil::createDescriptorImageInfo(normal.imageView, normal.sampler));

                if (materialUsesDisplacedTexture(model.materials[i])) {
                    auto& disp = findTexture(model.materials[i].occlusionTexture.index);
                    boundTextures.push_back(vkutil::createDescriptorImageInfo(disp.imageView, disp.sampler));
                    boundBuffers.push_back(vkutil::createDescriptorBufferInfo(constantsBuffers.buffers[0], 0, sizeof(glm::int32_t)));
                } else {
                    // We map the normal texture as both normal texture and as the height map.
                    // Vulkan requires that we bind all textures even if we don't use them ...
                    boundTextures.push_back(boundTextures.back());
                    boundBuffers.push_back(vkutil::createDescriptorBufferInfo(constantsBuffers.buffers[1], 0, sizeof(glm::int32_t)));
                }
            }
        }

        if (desiredLayout != VK_NULL_HANDLE) {
            materialDSet[i] =
                VulkanHelper::createDescriptorSetsFromLayout(*device, descriptorPool, desiredLayout, 1)[0];

            std::vector<VkWriteDescriptorSet> writes;
            size_t id = 0;
            for (id = 0; id < boundTextures.size(); id++) {
                writes.push_back(vkutil::createDescriptorWriteSampler(boundTextures[id], materialDSet[i], id));
            }

            for (size_t j = 0; j < boundBuffers.size(); j++, id++) {
                writes.push_back(vkutil::createDescriptorWriteUBO(boundBuffers[j], materialDSet[i], id));
            }

            device->writeDescriptorSets(writes);
        }
    }
}

RequiredDescriptors Scene::getNumDescriptors() {
    return RequiredDescriptors{
            .requireUniformBuffers = (uint) meshTransforms.size() + (uint)model.materials.size() + MAX_FRAMES_IN_FLIGHT,
            .requireSamplers = (uint)model.materials.size() * 3,
    };
}

void Scene::setupBuffers() {
    unsigned long numBuffers = model.buffers.size();
    buffers.resize(numBuffers);
    for (unsigned long i = 0; i < numBuffers; ++i) {
        buffers[i].uploadData(device, model.buffers[i].data,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    for (auto node: model.scenes[model.defaultScene].nodes) {
        generateTransforms(node, glm::mat4(1.f), MAX_RECURSION);
    }

    setupUniformBuffers();
}

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
    } else if (node.camera >= 0) {
        if (model.cameras[node.camera].type == "perspective") {
            auto perspective = model.cameras[node.camera].perspective;
            cameras.push_back({
                                      node.name,
                                      newTransform,
                                      static_cast<float>(perspective.yfov),
                                      static_cast<float>(perspective.znear),
                                      static_cast<float>(perspective.zfar)
                              });
        }
    }

    for (int child: node.children) {
        generateTransforms(child, newTransform, maxRecursion - 1);
    }
}

void Scene::setupUniformBuffers() {
    for (auto [mesh, transforms]: meshTransforms) {
        buffersMap[mesh] = buffers.size();
        buffers.push_back({});
        buffers.back().uploadData(device, transforms, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }
    if (lights.size() > 0) {
        lightsBuffer = buffers.size();
        buffers.push_back({});
        buffers.back().uploadData(device, lights,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    materialBuffer.allocate(device, sizeof(MaterialSettings), MAX_FRAMES_IN_FLIGHT);

    constantsBuffers.allocate(device, sizeof(glm::int32_t), 2);
    // Just two buffers with the constants 0 and 1, allowing us to reuse the shader ..
    for (glm::int32_t v = 0; v < 2; v++) {
        constantsBuffers.update(&v, sizeof(v), v);
    }
}

std::pair<VkBuffer, size_t> Scene::getPointLights() {
    return {buffers[lightsBuffer].buffer, lights.size()};
}

void Scene::destroyBuffers() {
    for (auto buffer: buffers) buffer.destroy(device);
    materialBuffer.destroy(device);
    constantsBuffers.destroy(device);
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

VkVertexInputBindingDescription Scene::getVertexBindingDescription(int accessor, int bindingId) {
    VkVertexInputBindingDescription bindingDescription;

    bindingDescription.binding = bindingId;
    bindingDescription.stride = VulkanHelper::strideFromGltfType(
            model.accessors[accessor].type,
            model.accessors[accessor].componentType,
            model.bufferViews[model.accessors[accessor].bufferView].byteStride);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

void Scene::ensureDescriptorSetLayouts() {
    if (uboDescriptorSetLayout == VK_NULL_HANDLE) {
        uboDescriptorSetLayout = device->createDescriptorSetLayout({
            vkutil::createSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
        });
    }

    if (albedoDSLayout == VK_NULL_HANDLE) {
        albedoDSLayout = device->createDescriptorSetLayout({
            vkutil::createSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
        });
    }

    if (albedoDisplacementDSLayout == VK_NULL_HANDLE) {
        albedoDisplacementDSLayout = device->createDescriptorSetLayout({
            vkutil::createSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            vkutil::createSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            vkutil::createSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT),
            vkutil::createSetLayoutBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
        });
    }

    if (materialsSettingsLayout == VK_NULL_HANDLE) {
        materialsSettingsLayout = device->createDescriptorSetLayout({
            vkutil::createSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT),
        });
    }
}

void Scene::destroyDescriptorSetLayout() {
    vkDestroyDescriptorSetLayout(*device, uboDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, albedoDSLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, albedoDisplacementDSLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, materialsSettingsLayout, nullptr);
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

static void setFromCamera(glm::vec3& lookAt, glm::vec3& position, glm::vec3& up,
    float& fovy, float& near, float& far, CameraData const& camera);

void Scene::computeDefaultCameraPos(glm::vec3 &lookAt, glm::vec3 &position, glm::vec3 &up, float &fovy, float &near, float &far) {
    if (!this->cameras.empty()) {
        setFromCamera(lookAt, position, up, fovy, near, far, cameras[0]);
        return;
    }

    // Compute bbox of the meshes, point to the middle and have a small distance
    // This works only for small test models, for bigger models, export a camera!
    glm::vec3 min, max;
    calculateBoundingBox(model, min, max);

    fovy = 45.0f;
    lookAt = (min + max) / 2.0f;
    float R = glm::length(max - min);
    R /= std::tan(fovy * M_PI / 360);
    R *= 0.6;

    position = glm::vec3(lookAt.x, lookAt.y + R, lookAt.z + R);
    near = 0.1f;
    far = 1000.f;
    up = glm::vec3(0, 0, 1);
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

void Scene::setupTextures() {
    const auto& loadTexture = [&] (int textureIdx) {

        // We have a texture here
        const tinygltf::Texture &gTexture = model.textures[textureIdx];
        if (textures.count(gTexture.source)) {
            return;
        }

        auto& image = model.images[gTexture.source];
        if (image.width <= 0 || image.height <= 0) {
            throw std::runtime_error("Image with negative dimensions, maybe a missing asset!");
        }

        textures[gTexture.source] = uploadGLTFImage(device, image);
        textures[gTexture.source].imageView =
            device->createImageView(textures[gTexture.source].image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
        textures[gTexture.source].sampler = VulkanHelper::createSampler(device, true);
    };

    for (size_t i = 0; i < model.materials.size(); i++) {
        const auto &material = model.materials[i];

        auto it = material.values.find(BASE_COLOR_TEXTURE);
        if (it != material.values.end()) {
            loadTexture(it->second.TextureIndex());
        }

        if (materialUsesNormalTexture(material)) {
            loadTexture(material.normalTexture.index);
        }

        if (materialUsesDisplacedTexture(material)) {
            loadTexture(material.occlusionTexture.index);
        }
    }
}

void Scene::destroyTextures() {
    for (auto &[idx, tex]: textures) {
        vkDestroyImageView(*device, tex.imageView, nullptr);
        vkDestroyImage(*device, tex.image, nullptr);
        vkDestroySampler(*device, tex.sampler, nullptr);
        vkFreeMemory(*device, tex.memory, nullptr);
    }
}

void Scene::destroyAll() {
    destroyDescriptorSetLayout();
    destroyTextures();
    destroyBuffers();
    pipelines.clear();
}

void Scene::createPipelines(VkRenderPass renderPass, VkDescriptorSetLayout mvpLayout, bool forceRecompile) {
    pipelines.clear();
    for (auto &[descr, _]: meshPrimitivesWithPipeline) {
        createPipelinesWithDescription(descr, renderPass, mvpLayout, forceRecompile);
    }
}

static ShaderList selectShaders(const PipelineDescription &descr) {
    if (descr.vertexFixedColorAccessor.has_value()) {
        return ShaderList {
            {VK_SHADER_STAGE_VERTEX_BIT,   "shaders/shader.vert"},
            {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/shader.frag"},
        };
    }

    if (descr.useNormalMap) {
        return ShaderList {
            {VK_SHADER_STAGE_VERTEX_BIT,   "shaders/displacement.vert"},
            {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/displacement.frag"},
        };
    }

    if (descr.useSSR) {
        return ShaderList {
            {VK_SHADER_STAGE_VERTEX_BIT,   "shaders/simple-texture.vert"},
            {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/reflection-texture.frag"},
        };
    }

    return ShaderList {
        {VK_SHADER_STAGE_VERTEX_BIT,   "shaders/simple-texture.vert"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/simple-texture.frag"},
    };
}

void Scene::createPipelinesWithDescription(PipelineDescription descr, VkRenderPass renderPass,
                                           VkDescriptorSetLayout mvpLayout, bool forceRecompile) {
    if (pipelines.count(descr)) {
        return;
    }

    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    const auto &addAttributeAndBinding = [&](int location, int binding, int accessor) {
        VkVertexInputAttributeDescription description{};
        description.binding = binding;
        description.location = location;
        description.format = VulkanHelper::gltfTypeToVkFormat(model.accessors[accessor].type,
                                                              model.accessors[accessor].componentType,
                                                              model.accessors[accessor].normalized);
        description.offset = 0;
        attributeDescriptions.push_back(description);
        bindingDescriptions.push_back(getVertexBindingDescription(accessor, binding));
    };

    if (!descr.vertexPosAccessor) {
        throw std::runtime_error("Unsupported mesh: we require vertex position for all vertices!");
    }

    if (!descr.vertexNormalAccessor) {
        throw std::runtime_error("Unsupported mesh: we require normals for all vertices!");
    }

    ensureDescriptorSetLayouts();
    std::vector<VkDescriptorSetLayout> dsLayouts{mvpLayout, uboDescriptorSetLayout};
    addAttributeAndBinding(0, 0, *descr.vertexPosAccessor);
    addAttributeAndBinding(2, 2, *descr.vertexNormalAccessor);
    if (descr.vertexFixedColorAccessor.has_value()) {
        addAttributeAndBinding(1, 1, *descr.vertexFixedColorAccessor);
    } else if (descr.vertexTexcoordsAccessor.has_value()) {
        addAttributeAndBinding(1, 1, *descr.vertexTexcoordsAccessor);

        if (descr.useNormalMap) {
            dsLayouts.push_back(albedoDisplacementDSLayout);
            dsLayouts.push_back(materialsSettingsLayout);
        } else {
            dsLayouts.push_back(albedoDSLayout);
        }
    } else {
        throw std::runtime_error("Mesh primitive without color or texcoords is not supported by shaders!");
    }

    PipelineParameters params;
    params.shadersList = selectShaders(descr);
    params.recompileShaders = forceRecompile;
    params.vertexAttributeDescription = attributeDescriptions;
    params.vertexInputDescription = bindingDescriptions;
    params.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    params.extent = swapchain->renderSize();

    // We don't want any blending for the color attachments (-1 for the depth attachment)
    params.blending.assign(GBufferTarget::NumAttachments - 1, {});
    params.useDepthTest = true;

    params.descriptorSetLayouts = dsLayouts;
    pipelines[descr] = std::make_unique<GraphicsPipeline>(device, renderPass, 0, params);
}

static void setFromCamera(glm::vec3& lookAt, glm::vec3& position, glm::vec3& up,
    float& fovy, float& near, float& far, CameraData const& camera)
{
    lookAt = glm::vec3(camera.view * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f));
    position = glm::vec3(camera.view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    up = glm::vec3(camera.view * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    fovy = glm::degrees(camera.yfov);
    near = camera.znear;
    far = camera.zfar;
}

void Scene::cameraButtons(glm::vec3& lookAt, glm::vec3& position, glm::vec3& up,
    float& fovy, float& near, float& far)
{
    for (auto const &camera: cameras) {
        if (ImGui::Button(camera.name.c_str())) {
            setFromCamera(lookAt, position, up, fovy, near, far, camera);
        }
    }
}

void Scene::updateBuffers() {
    materialBuffer.update(&materialSettings, sizeof(materialSettings), swapchain->currentFrame);
}

void Scene::drawImGUIMaterialSettings() {
    if (ImGui::CollapsingHeader("Material Settings")) {
        ImGui::Checkbox("Enable Inverse Displacement Mapping", (bool*)&materialSettings.enableInverseDisplacement);
        ImGui::Checkbox("Enable Linear Approximation", (bool*)&materialSettings.enableLinearApprox);
        ImGui::SliderInt("Raymarching Steps", &materialSettings.raymarchSteps, 1, 1000);
        ImGui::SliderFloat("Height Scale", &materialSettings.heightScale, 1e-6, 0.1);
        ImGui::Checkbox("Use gamma-corrected inverted depth", (bool*)&materialSettings.useInvertedFormat);
    }
}
