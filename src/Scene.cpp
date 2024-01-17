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
#include <numeric>

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

static bool materialIsOpaque(tinygltf::Material &material) {
    return material.alphaMode == "OPAQUE"; // could use double-sided parameter instead
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

    descr.isOpaque = materialIsOpaque(material);

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

    for (size_t i = 0; i < model.meshes.size(); i++) {
        addLoD(i);
    }

    for (auto &[meshName, lodList]: lods) {
        std::sort(lodList.begin(), lodList.end());
        // remove or restrict 0-to-infinity lods if more lods are present
        if (lodList.size() > 1) {
            auto globalLoD = std::find_if(lodList.begin(), lodList.end(),
                         [](LoD lod){return lod.dist_min == 0 && isinff(lod.dist_max) != 0;}
                         );
            if (std::find_if(lodList.begin(), lodList.end(),
                             [](LoD lod){return lod.dist_min == 0 && isinff(lod.dist_max) == 0;}) != lodList.end()) {
                lodList.erase(globalLoD);
            } else {
                float minimumNonZero = std::accumulate(
                        lodList.begin(), lodList.end(), INFINITY,
                        [](float previous, LoD current){
                            return current.dist_min > 0 ? std::fmin(previous, current.dist_min) : previous;
                        });
                globalLoD->dist_max = minimumNonZero;
            }

        }
    }


    // We precompute a list of mesh primitives to be rendered with each of the generated programs.
    for (auto [basename, lodList]: lods) {
        for (auto lod: lodList) {
            for (size_t j = 0; j < model.meshes[lod.mesh].primitives.size(); j++) {
                if (model.meshes[lod.mesh].primitives[j].material < 0) {
                    std::cout << "Unsupported primitive meshId=" << lod.mesh << " primitiveId=" << j
                              << ": no material specified." << std::endl;
                    continue;
                }

                auto descr = getPipelineDescriptionForPrimitive(model.meshes[lod.mesh].primitives[j]);

                if (!descr.vertexTexcoordsAccessor.has_value() && !descr.vertexFixedColorAccessor.has_value()) {
                    std::cout << "Unsupported primitive meshId=" << lod.mesh << " primitiveId=" << j
                              << ": no texture or vertex color specified." << std::endl;
                    continue;
                }

                meshPrimitivesWithPipeline[descr][lod].emplace_back(j);
            }
        }
    }
}

void Scene::recordCommandBufferCompute(VkCommandBuffer commandBuffer, glm::vec3 cameraPosition) {
    // update lods
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, updateLoDsPipeline->pipeline);
    for (auto &[meshName, meshLods]: lods) {
        if (meshLods.size() > 1) {
            uint32_t n = meshTransforms[meshNameMap[meshName]].size();
            for (int lodIndex = 0; lodIndex < meshLods.size(); lodIndex++) {
                auto lod = meshLods[lodIndex];
                bool hasHigher = lodIndex < meshLods.size() - 1;
                bool hasLower = lodIndex > 0;
                float lodDistMax = lod.dist_max;
                float lodDistMin = lod.dist_min;
                LodUpdatePushConstants constants = {{hasHigher, hasLower, lodDistMax, lodDistMin}, cameraPosition};
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, updateLoDsPipeline->layout,
                                        0, 1, &updateLoDsDescriptorSets[lodComputeDescriptorSetsMap[std::pair(meshNameMap[meshName],lodIndex)]], 0, nullptr);
                vkCmdPushConstants(commandBuffer, updateLoDsPipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(constants), &constants);
                vkCmdDispatch(commandBuffer, n, 1, 1);
            }
        }
    }
    VkMemoryBarrier lodUpdateBarrier{};
    lodUpdateBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    lodUpdateBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    lodUpdateBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, {},
                         1, &lodUpdateBarrier, 0, nullptr, 0, nullptr);
    // remove invalid transforms
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compressLoDsPipeline->pipeline);
    for (auto &[meshName, meshLods]: lods) {
        if (meshLods.size() > 1) {
            uint32_t n = meshTransforms[meshNameMap[meshName]].size();
            for (int lodIndex = 0; lodIndex < meshLods.size(); lodIndex++) {
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compressLoDsPipeline->layout,
                                        0, 1, &compressLoDsDescriptorSets[lodComputeDescriptorSetsMap[std::pair(meshNameMap[meshName],lodIndex)]], 0, nullptr);
                vkCmdDispatch(commandBuffer, n, 1, 1);
            }
        }
    }
    VkMemoryBarrier lodCompressionBarrier{};
    lodCompressionBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    lodCompressionBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    lodCompressionBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, {},
                         1, &lodCompressionBarrier, 0, nullptr, 0, nullptr);
    VkBufferCopy instanceCountRegion{};
    instanceCountRegion.size = sizeof(VkDrawIndexedIndirectCommand::instanceCount);
    instanceCountRegion.srcOffset = sizeof(uint32_t);
    instanceCountRegion.dstOffset = offsetof(VkDrawIndexedIndirectCommand, instanceCount);
    VkBufferCopy countToSizeRegion{};
    countToSizeRegion.size = sizeof(uint32_t);
    countToSizeRegion.srcOffset = sizeof(uint32_t);
    countToSizeRegion.dstOffset = 0;
    for (auto [meshName, lodList]: lods) {
        if (lodList.size() <= 1) continue; // only update loded meshes
        for (int i = 0; i < lodList.size(); i++) {
            auto metaBuffer = buffers[lodMetaBuffersMap[std::pair(meshNameMap[meshName], i)]];
            auto meshIndex = lodList[i].mesh;
            auto mesh = model.meshes[meshIndex];
            for (int j = 0; j < mesh.primitives.size(); j++) {
                auto drawCommandBuffer = buffers[lodIndirectDrawBufferMap[std::pair(meshIndex, j)]];
                vkCmdCopyBuffer(commandBuffer, metaBuffer.buffer, drawCommandBuffer.buffer, 1, &instanceCountRegion);
                // set size equal to count. this is necessary, since invalid transforms at the end would not reduce size
                vkCmdCopyBuffer(commandBuffer, metaBuffer.buffer, metaBuffer.buffer, 1, &countToSizeRegion);
            }
        }
    }
    VkMemoryBarrier drawCountCopyBarrier{};
    drawCountCopyBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    drawCountCopyBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    drawCountCopyBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, {},
                         1, &drawCountCopyBarrier, 0, nullptr, 0, nullptr);
}

void Scene::recordCommandBufferDraw(VkCommandBuffer commandBuffer, VkDescriptorSet mvpSet) {
    // Draw all primitives with all available graphicsPipelines.
    for (auto &[programDescr, lodMeshMap]: meshPrimitivesWithPipeline) {
        auto &pipeline = graphicsPipelines[programDescr];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
        VulkanHelper::setFullViewportScissor(commandBuffer, swapchain->renderSize());

        for (auto &[lod, primitivesList]: lodMeshMap) {
            // bind transformations for each mesh
            bindingDescriptorSets.clear();
            bindingDescriptorSets.push_back(mvpSet);
            bindingDescriptorSets.push_back(
                    meshTransformsDescriptorSets[descriptorSetsMap[lod]]);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, 0,
                                    bindingDescriptorSets.size(), bindingDescriptorSets.data(), 0, nullptr);

            for (auto &primitiveId: primitivesList) {
                // Bind the specific texture for each primitive
                renderPrimitiveInstances(lod.mesh, primitiveId, commandBuffer, programDescr, pipeline->layout);
            }
        }
    }
}

void Scene::renderPrimitiveInstances(int meshId, int primitiveId, VkCommandBuffer commandBuffer,
                                     const PipelineDescription &descr, VkPipelineLayout pipelineLayout) {
    //if (meshTransforms.find(meshId) == meshTransforms.end()) return; // 0 instances. skip. todo is this needed for lods?

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
        auto indexBufferType = VulkanHelper::gltfTypeToVkIndexType(
                model.accessors[indexAccessorIndex].componentType);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, indexBufferOffset, indexBufferType);
        vkCmdDrawIndexedIndirect(commandBuffer, buffers[lodIndirectDrawBufferMap[std::pair(meshId, primitiveId)]].buffer,
                                 0, 1, 0);
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
    meshTransformsDescriptorSets = VulkanHelper::createDescriptorSetsFromLayout(
            *device, descriptorPool, meshTransformsDescriptorSetLayout, getNumLods());

    updateLoDsDescriptorSets = VulkanHelper::createDescriptorSetsFromLayout(
            *device, descriptorPool, lodUpdateDescriptorSetLayout, getNumLods());

    compressLoDsDescriptorSets = VulkanHelper::createDescriptorSetsFromLayout(
            *device, descriptorPool, lodCompressDescriptorSetLayout, getNumLods());

    int transformsDescriptorIndex = 0;
    int lodComputeDescriptorIndex = 0;
    for (auto [meshName, lodList]: lods) {
        for (int lodIndex = 0; lodIndex < lodList.size(); lodIndex++) {
            const std::pair<int, int> &lodIdentifier = std::pair(meshNameMap[meshName], lodIndex);
            auto meshTransformsBufferIndex = lodTransformsBuffersMap[lodIdentifier];
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = buffers[meshTransformsBufferIndex].buffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(ModelTransform) * meshTransforms[meshNameMap[meshName]].size();

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            const LoD &lod = lodList[lodIndex];
            descriptorSetsMap[lod] = transformsDescriptorIndex++;
            descriptorWrite.dstSet = meshTransformsDescriptorSets[descriptorSetsMap[lod]];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;
            descriptorWrite.pImageInfo = nullptr; // Optional
            descriptorWrite.pTexelBufferView = nullptr; // Optional

            vkUpdateDescriptorSets(*device, 1, &descriptorWrite, 0, nullptr);

            if (lodList.size() > 1) {
                // todo maybe clean this mess up
                lodComputeDescriptorSetsMap[lodIdentifier] = lodComputeDescriptorIndex++;

                auto lodTransformsBufferIndex = lodTransformsBuffersMap[lodIdentifier];
                auto lodMetaBufferIndex = lodMetaBuffersMap[lodIdentifier];
                auto lodUpIdentifier = std::pair(meshNameMap[meshName],
                                                 lodIndex + 1 < lodList.size() ? lodIndex + 1 : lodIndex);
                auto lodDownIdentifier = std::pair(meshNameMap[meshName],
                                                   lodIndex - 1 >= 0 ? lodIndex - 1 : lodIndex);
                auto upLodTransformsBufferIndex = lodTransformsBuffersMap[lodUpIdentifier];
                auto upLodMetaBufferIndex = lodMetaBuffersMap[lodUpIdentifier];
                auto downLodTransformsBufferIndex = lodTransformsBuffersMap[lodDownIdentifier];
                auto downLodMetaBufferIndex = lodMetaBuffersMap[lodDownIdentifier];
                auto trafoSize = sizeof(ModelTransform) * meshTransforms[meshNameMap[meshName]].size();
                auto metaSize = ((meshTransforms[meshNameMap[meshName]].size() / 32 + 1) + 2) * sizeof(glm::uint);

                VkDescriptorBufferInfo transformsBufferInfo{};
                transformsBufferInfo.buffer = buffers[lodTransformsBufferIndex].buffer;
                transformsBufferInfo.offset = 0;
                transformsBufferInfo.range = trafoSize;

                VkDescriptorBufferInfo metaBufferInfo{};
                metaBufferInfo.buffer = buffers[lodMetaBufferIndex].buffer;
                metaBufferInfo.offset = 0;
                metaBufferInfo.range = metaSize;

                VkDescriptorBufferInfo upTransformsBufferInfo{};
                upTransformsBufferInfo.buffer = buffers[upLodTransformsBufferIndex].buffer;
                upTransformsBufferInfo.offset = 0;
                upTransformsBufferInfo.range = trafoSize;

                VkDescriptorBufferInfo upMetaBufferInfo{};
                upMetaBufferInfo.buffer = buffers[upLodMetaBufferIndex].buffer;
                upMetaBufferInfo.offset = 0;
                upMetaBufferInfo.range = metaSize;

                VkDescriptorBufferInfo downTransformsBufferInfo{};
                downTransformsBufferInfo.buffer = buffers[downLodTransformsBufferIndex].buffer;
                downTransformsBufferInfo.offset = 0;
                downTransformsBufferInfo.range = trafoSize;

                VkDescriptorBufferInfo downMetaBufferInfo{};
                downMetaBufferInfo.buffer = buffers[downLodMetaBufferIndex].buffer;
                downMetaBufferInfo.offset = 0;
                downMetaBufferInfo.range = metaSize;

                std::vector<VkDescriptorBufferInfo> updateBufferInfos {
                    upMetaBufferInfo,upTransformsBufferInfo,
                    metaBufferInfo,transformsBufferInfo,
                    downMetaBufferInfo, downTransformsBufferInfo
                };

                VkWriteDescriptorSet descriptorWriteUpdate{};
                descriptorWriteUpdate.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWriteUpdate.dstSet = updateLoDsDescriptorSets[lodComputeDescriptorSetsMap[lodIdentifier]];
                descriptorWriteUpdate.dstBinding = 0;
                descriptorWriteUpdate.dstArrayElement = 0;
                descriptorWriteUpdate.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWriteUpdate.descriptorCount = updateBufferInfos.size();
                descriptorWriteUpdate.pBufferInfo = updateBufferInfos.data();
                descriptorWriteUpdate.pImageInfo = nullptr; // Optional
                descriptorWriteUpdate.pTexelBufferView = nullptr; // Optional

                vkUpdateDescriptorSets(*device, 1, &descriptorWriteUpdate, 0, nullptr);

                std::vector<VkDescriptorBufferInfo> compressBufferInfos {
                    metaBufferInfo, transformsBufferInfo
                };

                VkWriteDescriptorSet descriptorWriteCompress{};
                descriptorWriteCompress.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWriteCompress.dstSet = compressLoDsDescriptorSets[lodComputeDescriptorSetsMap[lodIdentifier]];
                descriptorWriteCompress.dstBinding = 0;
                descriptorWriteCompress.dstArrayElement = 0;
                descriptorWriteCompress.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWriteCompress.descriptorCount = compressBufferInfos.size();
                descriptorWriteCompress.pBufferInfo = compressBufferInfos.data();
                descriptorWriteCompress.pImageInfo = nullptr; // Optional
                descriptorWriteCompress.pTexelBufferView = nullptr; // Optional

                vkUpdateDescriptorSets(*device, 1, &descriptorWriteCompress, 0, nullptr);
            }
        }
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
            .requireUniformBuffers = getNumLods() * 2 /*transforms, meta*/ +
                    (unsigned int) model.materials.size() + MAX_FRAMES_IN_FLIGHT,
            .requireSamplers = (unsigned int) model.materials.size() * 3,
    };
}

unsigned int Scene::getNumLods() {
    return std::accumulate(lods.begin(), lods.end(), 0u,
                           [](const unsigned int previous, const std::pair<std::string, std::vector<LoD>> &p) {
                               return previous + p.second.size();
                           });
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

    setupStorageBuffers();
    setupPrimitiveDrawBuffers();
}

void Scene::generateTransforms(int nodeIndex, glm::mat4 oldTransform, int maxRecursion) {
    if (maxRecursion <= 0) return;

    auto node = model.nodes[nodeIndex];
    if (node.mesh >= 0 && lods[model.meshes[node.mesh].name].size() == 0) return; // is lod mesh. ignore.

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
            auto name = light.Get("name").Get<std::string>();
            float wind = name.find("WIND") != name.npos;
            lights.push_back({glm::make_vec3(newTransform[3]), light_color, light_intensity, wind});
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

void Scene::setupPrimitiveDrawBuffers() {
    for (auto [mesh, transforms]: meshTransforms) {
        for (int i = 0; i < lods[model.meshes[mesh].name].size(); i++) {
            auto lod = lods[model.meshes[mesh].name][i];
            for (int j = 0; j < model.meshes[lod.mesh].primitives.size(); j++) {
                auto primitive = model.meshes[lod.mesh].primitives[j];
                if (primitive.indices >= 0) {
                    auto indexAccessorIndex = primitive.indices;
                    uint32_t numIndices = model.accessors[indexAccessorIndex].count;
                    VkDrawIndexedIndirectCommand drawCommand{};
                    drawCommand.indexCount = numIndices;
                    drawCommand.instanceCount = (i == 0) ? transforms.size() : 0;
                    lodIndirectDrawBufferMap[std::pair(lod.mesh, j)] = buffers.size();
                    buffers.push_back({});
                    buffers.back().uploadData(device, &drawCommand, sizeof(drawCommand),
                                              VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                }
            }
        }
    }
}

void Scene::setupStorageBuffers() {
    for (auto [mesh, transforms]: meshTransforms) {
        for (int i = 0; i < lods[model.meshes[mesh].name].size(); i++) {
            lodTransformsBuffersMap[std::pair(mesh, i)] = buffers.size();
            buffers.push_back({});
            if (i == 0) {
                buffers.back().uploadData(device, transforms, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            } else {
                buffers.back().createEmpty(device, sizeof(ModelTransform) * transforms.size(),
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            }
            if (lods[model.meshes[mesh].name].size() > 1) {  // if model is LoDed, we need metadata.
                lodMetaBuffersMap[std::pair(mesh, i)] = buffers.size();
                buffers.push_back({});
                size_t bufferSizeUints = (transforms.size() / 32 + 1) + 2;
                if (i == 0) {
                    std::vector<uint32_t> bufferData;
                    bufferData.resize(bufferSizeUints, static_cast<unsigned int>(~0));
                    bufferData[0] = transforms.size();
                    bufferData[1] = transforms.size();
                    buffers.back().uploadData(device, bufferData,
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                } else {
                    buffers.back().createEmpty(device, sizeof(glm::uint32) * bufferSizeUints,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                }
            }
        }
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

    VkVertexInputAttributeDescription windAttributeDescription{};
    windAttributeDescription.binding = 0;
    windAttributeDescription.location = 3;
    windAttributeDescription.format = VK_FORMAT_R32_SFLOAT;
    windAttributeDescription.offset = offsetof(LightData, wind);
    attributeDescriptions.push_back(windAttributeDescription);

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
    if (lodUpdateDescriptorSetLayout == VK_NULL_HANDLE) {
        lodUpdateDescriptorSetLayout = device->createDescriptorSetLayout(
            {
                vkutil::createSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
                vkutil::createSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
                vkutil::createSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
                vkutil::createSetLayoutBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
                vkutil::createSetLayoutBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
                vkutil::createSetLayoutBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
            }
        );
    }

    if (lodCompressDescriptorSetLayout == VK_NULL_HANDLE) {
        lodCompressDescriptorSetLayout = device->createDescriptorSetLayout(
            {
                vkutil::createSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
                vkutil::createSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT),
            }
        );
    }

    if (meshTransformsDescriptorSetLayout == VK_NULL_HANDLE) {
        meshTransformsDescriptorSetLayout = device->createDescriptorSetLayout({
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

void Scene::destroyDescriptorSetLayouts() {
    vkDestroyDescriptorSetLayout(*device, meshTransformsDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, albedoDSLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, albedoDisplacementDSLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, materialsSettingsLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, lodUpdateDescriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(*device, lodCompressDescriptorSetLayout, nullptr);
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
    const uint32_t imageSize = image.width * image.height * image.component * image.bits / 8;
    loadedTex.imageFormat = VulkanHelper::gltfImageToVkFormat(image);
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

    device->createImage(image.width, image.height, loadedTex.imageFormat,
                        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, loadedTex.image, loadedTex.memory);

    device->transitionImageLayout(loadedTex.image, loadedTex.imageFormat,
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(device, stagingBuffer, loadedTex.image, image.width, image.height);
    device->transitionImageLayout(loadedTex.image, loadedTex.imageFormat,
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
            device->createImageView(textures[gTexture.source].image, textures[gTexture.source].imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
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
    destroyDescriptorSetLayouts();
    destroyTextures();
    destroyBuffers();
    destroyPipelines();
}

void Scene::destroyPipelines() {
    graphicsPipelines.clear();
    updateLoDsPipeline.reset();
    compressLoDsPipeline.reset();
}

void Scene::createPipelines(VkRenderPass renderPass, VkDescriptorSetLayout mvpLayout, bool forceRecompile) {
    destroyPipelines();
    for (auto &[descr, _]: meshPrimitivesWithPipeline) {
        createPipelinesWithDescription(descr, renderPass, mvpLayout, forceRecompile);
    }

    ComputePipeline::Parameters updateParams{};
    updateParams.source = {VK_SHADER_STAGE_COMPUTE_BIT, "shaders/update_lods.comp"};
    updateParams.recompileShaders = forceRecompile;
    updateParams.descriptorSetLayouts = {lodUpdateDescriptorSetLayout};
    VkPushConstantRange lodInfoRange{};
    lodInfoRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    lodInfoRange.size = sizeof(LodUpdatePushConstants);
    updateParams.pushConstantRanges.push_back(lodInfoRange);
    this->updateLoDsPipeline = std::make_unique<ComputePipeline>(device, updateParams);

    ComputePipeline::Parameters compressParams{};
    compressParams.source = {VK_SHADER_STAGE_COMPUTE_BIT, "shaders/compress_lods.comp"};
    compressParams.recompileShaders = forceRecompile;
    compressParams.descriptorSetLayouts = {lodCompressDescriptorSetLayout};
    this->compressLoDsPipeline = std::make_unique<ComputePipeline>(device, compressParams);
}

static ShaderList selectShaders(const PipelineDescription &descr) {
    if (descr.vertexFixedColorAccessor.has_value()) {
        return ShaderList {
            {VK_SHADER_STAGE_VERTEX_BIT,   "shaders/shader.vert"},
            {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/shader.frag"},
        };
    }

    if (!descr.isOpaque) {
        return ShaderList {
                {VK_SHADER_STAGE_VERTEX_BIT,   "shaders/simple-texture-wind.vert"},
                {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/simple-texture.frag"},
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
    if (graphicsPipelines.count(descr)) {
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
    std::vector<VkDescriptorSetLayout> dsLayouts{mvpLayout, meshTransformsDescriptorSetLayout};
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
    params.backFaceCulling = descr.isOpaque;

    // We don't want any blending for the color attachments (-1 for the depth attachment)
    params.blending.assign(GBufferTarget::NumAttachments - 1, {});
    params.useDepthTest = true;

    params.descriptorSetLayouts = dsLayouts;
    graphicsPipelines[descr] = std::make_unique<GraphicsPipeline>(device, renderPass, 0, params);
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

void Scene::addLoD(int meshIndex) {
    tinygltf::Mesh mesh = model.meshes[meshIndex];
    auto name = mesh.name;
    LoD lod{meshIndex, 0, INFINITY};
    size_t index;
    if ((index = name.find("_LOD_")) != std::string::npos) {
        auto basename = name.substr(0, index);
        auto indexMin = index + 5;
        auto indexMax = name.find('_', index + 6);
        if (indexMax == std::string::npos) {
            auto minString = name.substr(indexMin);
            try {
                lod.dist_min = stof(minString);
            } catch (std::invalid_argument exception) {
                std::cout << "Invalid LOD distance for mesh" << name << std::endl;
                return;
            }
        } else {
            auto minString = name.substr(indexMin, indexMax);
            auto maxString = name.substr(indexMax + 1);
            try {
                lod.dist_min = stof(minString);
                lod.dist_max = stof(maxString);
            } catch (std::invalid_argument exception) {
                std::cout << "Invalid LOD distance for mesh" << name << std::endl;
                return;
            }
        }
        name = basename;
    } else {
        // is base mesh
        meshNameMap[name] = meshIndex;
    }
    lods[name].push_back(lod);
}
