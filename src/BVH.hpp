#pragma once

#include "DataBuffer.h"
#include "PhysicalDevice.h"
#include "Scene.h"
#include "tiny_gltf.h"
#include <glm/gtx/component_wise.hpp>
#include <vulkan/vulkan_core.h>
#include <iostream>
#include <chrono>
#include <numeric>
#include <iomanip>
#include <algorithm>
#include <cmath>

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

        cachePrecompute();
        int bvhDepth = constructBVH();

        // Free memory for augmented triangles.
        // We don't need them anymore so no point in keeping them in memory.
        freeCached();

        auto endTS = std::chrono::system_clock::now();
        std::cout << "Finished building BVH (tris=" << triangles.size() << ", maxdepth=" << bvhDepth
            << ") in " << std::chrono::duration_cast<std::chrono::milliseconds>(endTS-startTS).count()
            << "ms" << std::endl;

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

    static inline float midpoint(const Triangle& tri, int component) {
        return (tri.x[component] + tri.y[component] + tri.z[component]) / 3.0f;
    }

    // TODO: currently, this BVH construction is extremely simple.
    // We simply split triangles equally on both sides, and sort them by their center points.
    //
    // While this algorithm gives us a very balanced BVH (which is great since we don't need a large stack
    // in the compute shader), a better way would probably be to have a fixed constraint about the max stack
    // size, and if we have some room to tweak the BVH, we can use a different and better strategy.

    std::vector<Triangle> triangles;
    std::vector<glm::vec3> cachedMin;
    std::vector<glm::vec3> cachedMax;
    std::vector<float> midPoints[3];

    std::vector<BVHNode> bvh;

    int constructBVH() {
        bvh.push_back({});
        std::vector<int> triIndices(triangles.size());
        std::iota(triIndices.begin(), triIndices.end(), 0);

        auto fst = triIndices.begin();
        auto lst = triIndices.end();

        // Iterative splitting so that we can use parallelization
        // The idea is this: we have a list of ranges, that are iteratively splitted.
        // We do a BFS of the BVH.
        // This way, we can parallelize the construction of each level of the BVH.

        struct BVHPartialNode {
            int32_t start;
            int32_t end;
            int32_t curNodeIdx;
        };

        std::vector<BVHPartialNode> splitsSoFar = {{0, (int)triangles.size(), 0}};
        int depth = 0;

        while (!splitsSoFar.empty()) {
            std::vector<int32_t> middles(splitsSoFar.size());

            // Compute splits with a parallel for
            #pragma omp parallel for
            for (size_t i = 0; i < splitsSoFar.size(); i++) {
                iter start = triIndices.begin() + splitsSoFar[i].start;
                iter end = triIndices.begin() + splitsSoFar[i].end;
                if (end - start <= 1) {
                    setLeaf(splitsSoFar[i].curNodeIdx, *start);
                } else {
                    middles[i] = selectBVHSplit(start, end, depth) - triIndices.begin();
                }
            }

            // Update the BVH single-threadedly
            std::vector<BVHPartialNode> nextSplits;
            for (size_t i = 0; i < splitsSoFar.size(); i++) {
                iter start = triIndices.begin() + splitsSoFar[i].start;
                iter end = triIndices.begin() + splitsSoFar[i].end;
                if (end - start > 1) {
                    auto [leftIdx, rightIdx] = doSplit(splitsSoFar[i].curNodeIdx);
                    iter mid = triIndices.begin() + middles[i];
                    nextSplits.push_back({splitsSoFar[i].start, middles[i], leftIdx});
                    nextSplits.push_back({middles[i], splitsSoFar[i].end, rightIdx});
                }
            }

            depth++;
            splitsSoFar = std::move(nextSplits);
        }

        return computeBVH_AABB(0);
    }

    void cachePrecompute() {
        this->cachedMin.resize(triangles.size());
        this->cachedMax.resize(triangles.size());
        for (int component = 0; component < 3; component++) {
            midPoints[component].resize(triangles.size());
        }

#pragma omp parallel for
        for (size_t i = 0; i < triangles.size(); i++) {
            cachedMin[i] = glm::min(glm::min(triangles[i].x, triangles[i].y), triangles[i].z);
            cachedMax[i] = glm::max(glm::max(triangles[i].x, triangles[i].y), triangles[i].z);
            for (int component = 0; component < 3; component++) {
                midPoints[component][i] = midpoint(triangles[i], component);
            }
        }
    }

    void freeCached() {
        cachedMin.clear();
        cachedMax.clear();
        for (int component = 0; component < 3; component++) {
            this->midPoints[component].clear();
        }
    }

    using iter = std::vector<int>::iterator;

    iter selectBVHSplit(iter fst, iter lst, int depth) {
        const int size = lst - fst;
        const auto& updateBBox = [this] (glm::vec3& cmin, glm::vec3& cmax, int triIdx) {
            cmin = glm::min(cmin, cachedMin[triIdx]);
            cmax = glm::max(cmax, cachedMax[triIdx]);
        };

        static constexpr glm::vec3 MIN = glm::vec3(-1e9, -1e9, -1e9);
        static constexpr glm::vec3 MAX = glm::vec3(1e9, 1e9, 1e9);

        glm::vec3 bboxMin = MAX, bboxMax = MIN;
        std::for_each(fst, lst, [&] (int triIdx) {
            updateBBox(bboxMin, bboxMax, triIdx);
        });

        // Heuristic: split along the widest axis
        glm::vec3 dim = bboxMax - bboxMin;
        float mx = glm::compMax(dim);
        int sortBy = ((dim[0] == mx) ? 0 : ((dim[1] == mx) ? 1 : 2));

        // Sort based on x, y, z
        std::sort(fst, lst, [&] (int a, int b) {
            return midPoints[sortBy][a] < midPoints[sortBy][b];
        });
        // Split in the middle
        auto split = fst + size / 2;

        // We have to adhere to the maximal stack size in the shader and thus have to avoid unbalancing.
        // We can use the SAH heuristic only if we can guarantee that in the unlucky case, we still manage to
        // switch to balanced splits and not exceed the depth.
        int needBalancedDepth = std::ceil(std::log2(size)) + 1;
        static constexpr int MAX_STACK_SIZE = 32;

        if (depth + needBalancedDepth < MAX_STACK_SIZE - 2) {
            // pseudo-SAH, improve the split candidate.
            // Implementation: first compute prefix BBs.
            // Then, iterate forward and compute SAH heuristic.
            bboxMin = MAX;
            bboxMax = MIN;
            std::vector<glm::vec3> suffixBBMin(size), suffixBBMax(size);
            for (int i = size - 1; i >= 0; i--) {
                int triIdx = *(fst + i);
                updateBBox(bboxMin, bboxMax, triIdx);
                suffixBBMin[i] = bboxMin;
                suffixBBMax[i] = bboxMax;
            }

            // Forward pass, split in [0, i), [i, size)
            bboxMin = MAX;
            bboxMax = MIN;
            updateBBox(bboxMin, bboxMax, *fst);

            const auto& surfaceArea = [](glm::vec3 min, glm::vec3 max) {
                auto d = max - min;
                return d.x * d.y + d.x * d.z + d.y * d.z;
            };

            float optimal = 1e18;
            for (int i = 1; i < size; i++) {
                float S1 = surfaceArea(bboxMin, bboxMax);
                float S2 = surfaceArea(suffixBBMin[i], suffixBBMax[i]);
                float cost = i * S1 + (size - i) * S2;
                if (cost < optimal) {
                    optimal = cost;
                    split = fst + i;
                }

                int triIdx = *(fst + i);
                updateBBox(bboxMin, bboxMax, triIdx);
            }
        }

        return split;
    }

    std::pair<int, int> doSplit(int cur) {
        int leftIdx = bvh.size();
        bvh.push_back({});
        int rightIdx = bvh.size();
        bvh.push_back({});
        bvh[cur].left = leftIdx;
        bvh[cur].right = rightIdx;
        return {leftIdx, rightIdx};
    }

    void setLeaf(int curNodeIdx, int triIdx) {
        bvh[curNodeIdx].left = -triIdx;
        bvh[curNodeIdx].low = cachedMin[triIdx];
        bvh[curNodeIdx].high = cachedMax[triIdx];
    }

    int computeBVH_AABB(int cur) {
        int left = bvh[cur].left;
        int right = bvh[cur].right;
        if (left <= 0) return 0;

        int depthL = computeBVH_AABB(left);
        int depthR = computeBVH_AABB(right);
        bvh[cur].low = glm::min(bvh[left].low, bvh[right].low);
        bvh[cur].high = glm::max(bvh[left].high, bvh[right].high);
        return std::max(depthL, depthR) + 1;
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
                if (indexAccessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
                    indexAccessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
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

                const auto& getIndexArray = [&] (int idx) -> int {
                    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        auto indexArray = reinterpret_cast<const uint16_t*>(
                            &model.buffers[indexBView.buffer].data[indexBView.byteOffset]);
                        return indexArray[idx];
                    }
                    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        auto indexArray = reinterpret_cast<const uint32_t*>(
                            &model.buffers[indexBView.buffer].data[indexBView.byteOffset]);
                        return indexArray[idx];
                    }

                    throw std::runtime_error("Invalid component type");
                };


                auto vertexArray = reinterpret_cast<const float*>(
                    &model.buffers[posBView.buffer].data[posBView.byteOffset]);

                for (int _idx = 0; _idx < indexAccessor.count; _idx += 3) {
                    int idx0 = getIndexArray(_idx + 0);
                    int idx1 = getIndexArray(_idx + 1);
                    int idx2 = getIndexArray(_idx + 2);

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
