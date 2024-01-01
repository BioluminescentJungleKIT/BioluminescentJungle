#include "DataBuffer.h"
#include "VulkanHelper.h"
#include <vulkan/vulkan_core.h>

void StaticDataBuffer::uploadData(VulkanDevice* device, void* data,
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    VulkanHelper::createBuffer(*device, device->physicalDevice, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        properties, buffer, memory);
    VulkanHelper::uploadBuffer(*device, device->physicalDevice, size, buffer,
        data, device->commandPool, device->graphicsQueue);
    this->size = size;
}

void StaticDataBuffer::destroy(VulkanDevice* device)
{
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(*device, buffer, NULL);
        vkFreeMemory(*device, memory, NULL);
    }
}
