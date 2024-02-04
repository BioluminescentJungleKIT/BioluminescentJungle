// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#include "DataBuffer.h"
#include "VulkanHelper.h"
#include <vulkan/vulkan_core.h>

void DataBuffer::uploadData(VulkanDevice* device, void* data,
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    if (data == nullptr)
    {
        VulkanHelper::createBuffer(*device, device->physicalDevice, size, usage, properties, buffer, memory);
    } else
    {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VulkanHelper::createBuffer(*device, device->physicalDevice, size, usage, properties, buffer, memory);
        VulkanHelper::uploadBuffer(*device, device->physicalDevice, size, buffer,
            data, device->commandPool, device->graphicsQueue);
    }

    this->size = size;
}

void DataBuffer::destroy(VulkanDevice* device)
{
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(*device, buffer, NULL);
        vkFreeMemory(*device, memory, NULL);
    }
}
