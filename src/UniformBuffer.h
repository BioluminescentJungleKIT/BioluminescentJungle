#ifndef UNIFORM_BUFFER_H
#define UNIFORM_BUFFER_H

#include "PhysicalDevice.h"

/**
 * A helper class to manage uniform buffers.
 */
class UniformBuffer {
  public:
    void allocate(VulkanDevice* device, size_t size, size_t copies);
    void update(void* data, size_t size, int copyIdx);
    void destroy(VulkanDevice *device);

    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> memories;
    std::vector<void *> mappedPointer;
};

#endif /* end of include guard: UNIFORM_BUFFER_H */
