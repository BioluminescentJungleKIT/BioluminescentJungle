#pragma once

#include "PhysicalDevice.h"
#include <vulkan/vulkan_core.h>

/**
 * A helper class to manage buffers with static data.
 */
class DataBuffer {
  public:
    void uploadData(VulkanDevice* device, void* data, VkDeviceSize size,
                    VkBufferUsageFlags usage, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    void createEmpty(VulkanDevice* device, VkDeviceSize size,
                    VkBufferUsageFlags usage, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        std::vector<uint8_t> empty;
        empty.resize(size, 0);
        uploadData(device, empty, usage, properties);
    }

    template<class T>
    void uploadData(VulkanDevice* device, const std::vector<T>& data,
        VkBufferUsageFlags usage, VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    {
        if (data.size() > 0)
        {
            uploadData(device, (void*)data.data(), sizeof(T) * data.size(), usage, properties);
        }
    }

    VkDescriptorBufferInfo& getDescriptor() {
        this->descriptor = VkDescriptorBufferInfo {
            .buffer = buffer,
            .offset = 0,
            .range = size,
        };

        return descriptor;
    }

    void destroy(VulkanDevice* device);

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    size_t size = 0;

  private:
    VkDescriptorBufferInfo descriptor;
};
