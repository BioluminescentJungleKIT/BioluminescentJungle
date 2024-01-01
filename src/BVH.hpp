#pragma once

#include "DataBuffer.h"
#include "PhysicalDevice.h"
#include "Scene.h"
#include "tiny_gltf.h"
#include <vulkan/vulkan_core.h>
#include <iostream>
#include <chrono>

inline std::ostream &operator<<(std::ostream &out, const glm::vec3 &value) {
    out << std::setprecision(4) << "(" << value.x << "," << value.y << "," << value.z << ")";
    return out;
}

/**
 * Build a BVH for the given scene.
 *
 * We extract all triangles from all primitives and build our own buffer with them.
 */
class BVH {
    struct Triangle {
        glm::vec3 x alignas(16);
        glm::vec3 y alignas(16);
        glm::vec3 z alignas(16);
    };

    struct BVHNode {
        glm::vec3 low alignas(16);
        glm::vec3 high alignas(16);

        // If negative, indicates a leaf triangle node
        glm::int32_t left;
        glm::int32_t right;
    };

    static glm::vec3 transformVec(const glm::vec3& vec, const glm::mat4& mat) {
        glm::vec4 v(vec.x, vec.y, vec.z, 1.0);
        v = mat * v;
        return glm::vec3(v);
    }

    static Triangle transformTriangle(const Triangle& tri, const glm::mat4& mat) {
        return Triangle { transformVec(tri.x, mat), transformVec(tri.y, mat), transformVec(tri.z, mat) };
    }

  public:
    BVH(VulkanDevice *device, Scene *scene) {
        this->device = device;

        std::cout << "Starting building BVH" << std::endl;
        auto startTS = std::chrono::system_clock::now();

        auto triangles = extractTriangles(scene);

        std::cout << "Got triangles from the model: " << triangles.size() << std::endl;

        for (auto& tri : triangles) {
            std::cout << tri.x << " " << tri.y << " " << tri.z << std::endl;
        }

        auto endTS = std::chrono::system_clock::now();
        std::cout << "Finished building BVH in " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(endTS-startTS).count() << "ms" << std::endl;

        triangleBuffer.uploadData(device, triangles, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    VkDescriptorBufferInfo getBVHInfo() {
        return bvhBuffer.getDescriptor();
    }

    VkDescriptorBufferInfo getTriangleInfo() {
        return triangleBuffer.getDescriptor();
    }

    ~BVH() {
        bvhBuffer.destroy(device);
        triangleBuffer.destroy(device);
    }

  private:
    VulkanDevice *device;
    StaticDataBuffer bvhBuffer;
    StaticDataBuffer triangleBuffer;

    // Compute a list of all triangles in the model.
    std::vector<Triangle> extractTriangles(Scene *scene) {
        auto& model = scene->model;

        std::vector<Triangle> result;
        for (size_t meshId = 0; meshId < model.meshes.size(); meshId++) {
            if (!scene->meshTransforms.count(meshId)) {
                continue;
            }

            for (const auto& primitive: model.meshes[meshId].primitives) {
                if (primitive.indices < 0) {
                    continue;
                }

                if (primitive.attributes.count("POSITION") <= 0) {
                    continue;
                }


                auto& indexAccessor = model.accessors[primitive.indices];
                auto& indexBView = model.bufferViews[indexAccessor.bufferView];
                if (indexAccessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                {
                    std::cout << "Unsupported GLTF component type in index buffer!" << std::endl;
                    continue;
                }

                auto posAccessor = model.accessors[primitive.attributes.at("POSITION")];
                auto posBView = model.bufferViews[posAccessor.bufferView];

                if (posAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
                    posAccessor.type != TINYGLTF_TYPE_VEC3 ||
                    posAccessor.normalized)
                {
                    std::cout << "Currently, we support only Vec3 non-normalized floats for the BVH!" << std::endl;
                    continue;
                }

                auto indexArray = reinterpret_cast<const uint16_t*>(
                    &model.buffers[indexBView.buffer].data[indexBView.byteOffset]);

                auto vertexArray = reinterpret_cast<const float*>(
                    &model.buffers[posBView.buffer].data[posBView.byteOffset]);

                for (int _idx = 0; _idx < indexAccessor.count; _idx += 3) {
                    int idx0 = indexArray[_idx + 0];
                    int idx1 = indexArray[_idx + 1];
                    int idx2 = indexArray[_idx + 2];

                    Triangle tri{
                        glm::vec3(vertexArray[3 * idx0], vertexArray[3 * idx0 + 1], vertexArray[3 * idx0 + 2]),
                        glm::vec3(vertexArray[3 * idx1], vertexArray[3 * idx1 + 1], vertexArray[3 * idx1 + 2]),
                        glm::vec3(vertexArray[3 * idx2], vertexArray[3 * idx2 + 1], vertexArray[3 * idx2 + 2]),
                    };

                    for (auto& instance : scene->meshTransforms[meshId]) {
                        result.push_back(transformTriangle(tri, instance.model));
                    }
                }
            }
        }

        return result;
    }
};
