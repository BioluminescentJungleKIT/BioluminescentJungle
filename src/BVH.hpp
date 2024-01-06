#pragma once

#include "DataBuffer.h"
#include "PhysicalDevice.h"
#include "Scene.h"
#include "tiny_gltf.h"
#include <vulkan/vulkan_core.h>
#include <iostream>
#include <chrono>
#include <numeric>
#include <iomanip>
#include <algorithm>

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
  public:
    struct Triangle {
        glm::vec3 x alignas(16);
        glm::vec3 y alignas(16);
        glm::vec3 z alignas(16);
    };

    struct EmissiveTriangle {
        glm::vec3 x alignas(16);
        glm::vec3 y alignas(16);
        glm::vec3 z alignas(16);
        glm::vec4 emission alignas(16);
    };

    struct BVHNode {
        glm::vec3 low alignas(16);
        glm::vec3 high alignas(16);

        // If negative, indicates a leaf triangle node
        glm::int32_t left alignas(16);
        glm::int32_t right;
    };

    static glm::vec3 transformVec(const glm::vec3& vec, const glm::mat4& mat) {
        glm::vec4 v(vec.x, vec.y, vec.z, 1.0);
        v = mat * v;
        return glm::vec3(v);
    }

    template<class TriangleType>
    static TriangleType transformTriangle(const TriangleType& tri, const glm::mat4& mat) {
        TriangleType result = tri;
        result.x = transformVec(tri.x, mat);
        result.y = transformVec(tri.y, mat);
        result.z = transformVec(tri.z, mat);
        return result;
    }

  public:
    BVH(VulkanDevice *device, Scene *scene) {
        this->device = device;

        std::cout << "Starting building BVH" << std::endl;
        auto startTS = std::chrono::system_clock::now();

        this->triangles = extractTriangles<Triangle>(scene);
        constructBVH();
        //std::cout << "Got triangles from the model: " << triangles.size() << std::endl;
        //for (size_t i = 0; i < triangles.size(); i++) {
        //    std::cout << "idx=" << i << ": "
        //        << triangles[i].x << " " << triangles[i].y << " " << triangles[i].z << std::endl;
        //}

        //std::cout << "BVH has " << bvh.size() << std::endl;
        //for (size_t i = 0; i < bvh.size(); i++) {
        //    std::cout << "idx=" << i << ": " << bvh[i].low << " " << bvh[i].high <<
        //        " " << bvh[i].left << " " << bvh[i].right << std::endl;
        //}

        auto endTS = std::chrono::system_clock::now();
        std::cout << "Finished building BVH in " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(endTS-startTS).count() << "ms" << std::endl;

        triangleBuffer.uploadData(device, triangles, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        bvhBuffer.uploadData(device, bvh, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    VkDescriptorBufferInfo getBVHInfo() {
        return bvhBuffer.getDescriptor();
    }

    VkDescriptorBufferInfo getTriangleInfo() {
        return triangleBuffer.getDescriptor();
    }

    size_t getNTriangles() {
        return triangleBuffer.size / sizeof(Triangle);
    }

    ~BVH() {
        bvhBuffer.destroy(device);
        triangleBuffer.destroy(device);
    }

  private:
    VulkanDevice *device;
    DataBuffer bvhBuffer;
    DataBuffer triangleBuffer;

    static inline glm::vec3 midpoint(const Triangle& tri) {
        return (tri.x + tri.y + tri.z) / 3.0f;
    }

    // TODO: currently, this BVH construction is extremely simple.
    // We simply split triangles equally on both sides, and sort them by their center points.
    //
    // While this algorithm gives us a very balanced BVH (which is great since we don't need a large stack
    // in the compute shader), a better way would probably be to have a fixed constraint about the max stack
    // size, and if we have some room to tweak the BVH, we can use a different and better strategy.

    std::vector<Triangle> triangles;
    std::vector<BVHNode> bvh;

    void constructBVH() {
        bvh.push_back({});
        std::vector<int> triIndices(triangles.size());
        std::iota(triIndices.begin(), triIndices.end(), 0);
        constructBVHRec(triIndices.begin(), triIndices.end(), 0, 0);
    }

    using iter = std::vector<int>::iterator;
    void constructBVHRec(iter fst, iter lst, int curNodeIdx, int depth)
    {
        const size_t size = lst - fst;
        // Leaf
        if (size == 1) {
            bvh[curNodeIdx].left = -*fst;

            auto& tri = triangles[*fst];
            bvh[curNodeIdx].low = glm::min(glm::min(tri.x, tri.y), tri.z);
            bvh[curNodeIdx].high = glm::max(glm::max(tri.x, tri.y), tri.z);
            return;
        }

        // Sort based on x, y, z
        std::sort(fst, lst, [&] (int a, int b) {
            if (depth % 3 == 0) {
                return midpoint(triangles[a]).x < midpoint(triangles[b]).x;
            } else if (depth % 3 == 1) {
                return midpoint(triangles[a]).y < midpoint(triangles[b]).y;
            } else {
                return midpoint(triangles[a]).z < midpoint(triangles[b]).z;
            }
        });

        int leftIdx = bvh.size();
        bvh.push_back({});
        int rightIdx = bvh.size();
        bvh.push_back({});
        bvh[curNodeIdx].left = leftIdx;
        bvh[curNodeIdx].right = rightIdx;

        constructBVHRec(fst, fst + size / 2, leftIdx, depth + 1);
        constructBVHRec(fst + size / 2, lst, rightIdx, depth + 1);
        bvh[curNodeIdx].low = glm::min(bvh[leftIdx].low, bvh[rightIdx].low);
        bvh[curNodeIdx].high = glm::max(bvh[leftIdx].high, bvh[rightIdx].high);
    }

  public:
    // Compute a list of all triangles in the model.
    template<class TriangleType>
    static std::vector<TriangleType> extractTriangles(Scene *scene) {
        auto& model = scene->model;

        std::vector<TriangleType> result;
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

                if constexpr (std::is_same_v<TriangleType, EmissiveTriangle>) {
                    // Emissive only
                    int matId = primitive.material;
                    float highestEmission = 0;
                    if (matId >= 0) {
                        for (float f : model.materials[matId].emissiveFactor) {
                            highestEmission = std::max(highestEmission, f);
                        }
                    }

                    if (highestEmission <= 0) {
                        continue;
                    }
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

                    TriangleType tri;
                    tri.x = glm::vec3(vertexArray[3 * idx0], vertexArray[3 * idx0 + 1], vertexArray[3 * idx0 + 2]);
                    tri.y = glm::vec3(vertexArray[3 * idx1], vertexArray[3 * idx1 + 1], vertexArray[3 * idx1 + 2]);
                    tri.z = glm::vec3(vertexArray[3 * idx2], vertexArray[3 * idx2 + 1], vertexArray[3 * idx2 + 2]);

                    if constexpr (std::is_same_v<TriangleType, EmissiveTriangle>) {
                        auto& material = model.materials[primitive.material];
                        auto& emission = material.emissiveFactor;

#define KHR_emissive_strength "KHR_materials_emissive_strength"
                        float strength = 1.0;
                        if (material.extensions.contains(KHR_emissive_strength)) {
                            strength = material.extensions[KHR_emissive_strength]
                                .Get("emissiveStrength").Get<double>();
                        }

                        tri.emission = {emission[0], emission[1], emission[2], strength};
                    }

                    for (auto& instance : scene->meshTransforms[meshId]) {
                        result.push_back(transformTriangle(tri, instance.model));
                    }
                }
            }
        }

        return result;
    }
};
