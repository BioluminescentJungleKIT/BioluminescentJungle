#include "UniformBuffer.h"
#include "VulkanHelper.h"

void UniformBuffer::allocate(VulkanDevice* device, size_t size, size_t copies, VkBufferUsageFlags usageFlags) {
    buffers.resize(copies);
    memories.resize(copies);
    mappedPointer.resize(copies);

    for (int i = 0; i < copies; i++) {
        VulkanHelper::createBuffer(*device, device->physicalDevice, size, usageFlags,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffers[i], memories[i]);

        vkMapMemory(*device, memories[i], 0, size, 0, &mappedPointer[i]);
    }
}

void UniformBuffer::update(void* data, size_t size, uint32_t copyIdx) {
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

void UniformBuffer::copyTo(UniformBuffer dstBuffer, uint32_t srcIndex, uint32_t dstIndex, size_t size) {
    dstBuffer.update(mappedPointer[srcIndex], size, dstIndex);
}
