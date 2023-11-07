#include "Swapchain.h"
#include "VulkanHelper.h"
#include <algorithm>
#include <vulkan/vulkan_core.h>

void RenderTarget::init(VulkanDevice* device, int nrFrames) {
    this->device = device;
    this->nrFrames = nrFrames;
    imageViews.resize(nrFrames);
    framebuffers.resize(nrFrames);
}

void RenderTarget::destroyAll() {
    for (size_t i = 0; i < framebuffers.size(); i++) {
        vkDestroyFramebuffer(*device, framebuffers[i], nullptr);
    }
    framebuffers.clear();

    for (auto& set : imageViews) {
        for (auto& imageView : set) {
            vkDestroyImageView(*device, imageView, nullptr);
        }
    }
    imageViews.clear();

    for (auto& img : images) {
        vkDestroyImage(*device, img, nullptr);
    }
    images.clear();

    for (auto& mem : deviceMemories) {
        vkFreeMemory(*device, mem, nullptr);
    }
    deviceMemories.clear();
}

void RenderTarget::addAttachment(std::vector<VkImage> images, VkFormat fmt, VkImageAspectFlags flags) {
    assert(images.size() == nrFrames);
    for (size_t i = 0; i < nrFrames; i++) {
        imageViews[i].push_back(device->createImageView(images[i], fmt, flags));
    }
}

void RenderTarget::addAttachment(VkExtent2D extent, VkFormat fmt,
    VkImageUsageFlags usageFlags, VkImageAspectFlags aspectFlags) {

    for (int i = 0; i < nrFrames; i++) {
        VkImage image;
        VkDeviceMemory memory;

        device->createImage(extent.width, extent.height, fmt, VK_IMAGE_TILING_OPTIMAL, usageFlags,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, memory);
        deviceMemories.push_back(memory);
        images.push_back(image);
        imageViews[i].push_back(device->createImageView(image, fmt, aspectFlags));
    }
}

void RenderTarget::createFramebuffers(VkRenderPass renderPass, VkExtent2D extent) {
    for (size_t i = 0; i < nrFrames; i++) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(imageViews[i].size());
        framebufferInfo.pAttachments = imageViews[i].data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;
        VK_CHECK_RESULT(vkCreateFramebuffer(*device, &framebufferInfo, nullptr, &framebuffers[i]))
    }
}

void Swapchain::cleanupSwapChain() {
    defaultTarget.destroyAll();
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(*device, renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(*device, imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(*device, inFlightFences[i], nullptr);
    }

    vkDestroySwapchainKHR(*device, swapChain, nullptr);
}

void Swapchain::recreateSwapChain(VkRenderPass renderPass) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(*device);

    cleanupSwapChain();
    createSwapChain();
    createImageViews();
    createFramebuffersForRender(renderPass);
}

VkSurfaceFormatKHR Swapchain::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto &availableFormat: availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR Swapchain::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto &availablePresentMode: availablePresentModes) {
        if ((availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR && enableVSync) ||
            (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR && !enableVSync)) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                                        capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

void Swapchain::createSwapChain() {
    auto swapChainSupport = device->querySwapChainSupport(device->physicalDevice, surface);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    uint32_t queueFamilyIndices[] = {
        device->chosenQueues.graphicsFamily.value(),
        device->chosenQueues.presentFamily.value()
    };

    if (queueFamilyIndices[0] != queueFamilyIndices[1]) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0; // Optional
        createInfo.pQueueFamilyIndices = nullptr; // Optional
    }
    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK_RESULT(vkCreateSwapchainKHR(*device, &createInfo, nullptr, &swapChain))

    vkGetSwapchainImagesKHR(*device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(*device, swapChain, &imageCount, swapChainImages.data());
    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
}

void Swapchain::createImageViews() {
    defaultTarget.init(device, swapChainImages.size());
    defaultTarget.addAttachment(swapChainImages, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
}

void Swapchain::createFramebuffersForRender(VkRenderPass renderPass) {
    createDepthResources();
    defaultTarget.createFramebuffers(renderPass, swapChainExtent);
}

static VkFormat findSupportedFormat(VkPhysicalDevice device, const std::vector<VkFormat>& candidates) {
    const auto features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(device, format, &props);
        if ((props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

VkFormat Swapchain::chooseDepthFormat() {
    return findSupportedFormat(device->physicalDevice,
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT});
}

void Swapchain::createDepthResources() {
    defaultTarget.addAttachment(swapChainExtent, chooseDepthFormat(),
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
}

Swapchain::Swapchain(GLFWwindow* window, VkSurfaceKHR surface, VulkanDevice* device) {
    this->window = window;
    this->surface = surface;
    this->device = device;
    createSyncObjects();
    createSwapChain();
    createImageViews();
}

Swapchain::~Swapchain() {
    cleanupSwapChain();
}

std::optional<uint32_t> Swapchain::acquireNextImage(VkRenderPass renderPass) {
    vkWaitForFences(*device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(*device, swapChain, UINT64_MAX,
        imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain(renderPass);
        return {};
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        VK_CHECK_RESULT(result)
    }

    vkResetFences(*device, 1, &inFlightFences[currentFrame]);
    return imageIndex;
}

VkResult Swapchain::queuePresent(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    VK_CHECK_RESULT(vkQueueSubmit(device->graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]))
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr; // Optional

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    return vkQueuePresentKHR(device->presentQueue, &presentInfo);
}

void Swapchain::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_CHECK_RESULT(vkCreateSemaphore(*device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]))
        VK_CHECK_RESULT(vkCreateSemaphore(*device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]))
        VK_CHECK_RESULT(vkCreateFence(*device, &fenceInfo, nullptr, &inFlightFences[i]))
    }
}
