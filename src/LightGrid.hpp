#include "BVH.hpp"

// A LightGrid partitions the emissive triangles of a scene into fixed-size cells.
// At runtime, we select the nearest cells to the camera and use only light sources from those cells in the
// ReSTIR computation.
class LightGrid {
    static int quantize(float coord, float cellSize) {
        return std::floor(coord / cellSize);
    }

  public:
    LightGrid(VulkanDevice *device, Scene *scene, float cellSizeX, float cellSizeY) {
        this->device = device;
        this->cellSizeX = cellSizeX;
        this->cellSizeY = cellSizeY;

        auto emTris = BVH::extractTriangles<BVH::EmissiveTriangle>(scene);
        glm::vec3 min(1e9, 1e9, 1e9), max(-1e9, -1e9, -1e9);
        for (auto& tri : emTris) {
            min = glm::min(glm::min(tri.x, tri.y), glm::min(min, tri.z));
            max = glm::max(glm::max(tri.x, tri.y), glm::max(max, tri.z));
        }

        this->offX = -quantize(min.x, cellSizeX);
        this->offY = -quantize(min.y, cellSizeY);
        gridSizeX = quantize(max.x, cellSizeX) + 1 + offX;
        gridSizeY = quantize(max.y, cellSizeY) + 1 + offY;

        if (emTris.empty()) {
            gridSizeX = gridSizeY = 1;
        }

        std::vector<std::vector<std::vector<glm::int32>>> trianglesInCell(gridSizeX,
            std::vector<std::vector<glm::int32>>(gridSizeY));
        for (size_t i = 0; i < emTris.size(); i++) {
            int x = quantize(BVH::midpoint(emTris[i], 0), cellSizeX) + offX;
            int y = quantize(BVH::midpoint(emTris[i], 1), cellSizeY) + offY;
            trianglesInCell[x][y].push_back(i);
        }

        std::vector<glm::int32> linearizedTrianglesInCell;
        std::vector<glm::int32> cellOffsets(gridSizeX * gridSizeY);

        for (int i = 0; i < gridSizeX; i++) {
            for (int j = 0; j < gridSizeY; j++) {
                cellOffsets[i * gridSizeY + j] = linearizedTrianglesInCell.size();
              //  std::cout << debug(i) _ debug(j) _ debug(trianglesInCell[i][j].size()) << std::endl;
                for (auto& triIdx : trianglesInCell[i][j]) {
                    linearizedTrianglesInCell.push_back(triIdx);
                }
            }
        }
        // Sentinel value in the end to avoid branches in the shader
        cellOffsets.push_back(linearizedTrianglesInCell.size());

        this->emissiveTriangles.uploadData(device, emTris, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        this->gridCellContents.uploadData(device, linearizedTrianglesInCell, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        this->gridCellOffsets.uploadData(device, cellOffsets, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    ~LightGrid() {
        emissiveTriangles.destroy(device);
    }

    // The full list of emissive triangles
    DataBuffer emissiveTriangles;

    // The grid cells with triangle indices, linearized
    DataBuffer gridCellContents;
    DataBuffer gridCellOffsets;
    glm::int32 gridSizeX;
    glm::int32 gridSizeY;
    glm::float32 cellSizeX;
    glm::float32 cellSizeY;

    // Added to quantized X,Y in order to support "negative" indices
    glm::int32 offX;
    glm::int32 offY;

  private:
    VulkanDevice *device;
};
