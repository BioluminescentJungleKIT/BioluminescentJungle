#include "UniformBuffer.h"
#include "VulkanHelper.h"

void UniformBuffer::allocate(VulkanDevice* device, size_t size, size_t copies) {
    buffers.resize(copies);
    memories.resize(copies);
    mappedPointer.resize(copies);

    for (int i = 0; i < copies; i++) {
        VulkanHelper::createBuffer(*device, device->physicalDevice, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffers[i], memories[i]);

        vkMapMemory(*device, memories[i], 0, size, 0, &mappedPointer[i]);
    }
}

void UniformBuffer::update(void* data, size_t size, int copyIdx) {
    memcpy(mappedPointer[copyIdx], data, size);
}

void UniformBuffer::destroy(VulkanDevice *device) {
    for (auto& buffer : buffers) {
        vkDestroyBuffer(*device, buffer, nullptr);
    }

    for (auto& mem : memories) {
        vkFreeMemory(*device, mem, nullptr);
    }
}
