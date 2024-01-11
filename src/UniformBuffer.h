#ifndef UNIFORM_BUFFER_H
#define UNIFORM_BUFFER_H

#include "PhysicalDevice.h"

/**
 * A helper class to manage uniform buffers.
 */
class UniformBuffer {
  public:
    void allocate(VulkanDevice* device, size_t size, size_t copies,
        VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    void update(void* data, size_t size, uint32_t copyIdx);
    void destroy(VulkanDevice *device);

    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> memories;
    std::vector<void *> mappedPointer;

    void copyTo(UniformBuffer dstBuffer, uint32_t srcIndex, uint32_t dstIndex, size_t size);
};

#endif /* end of include guard: UNIFORM_BUFFER_H */
