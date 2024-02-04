// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#ifndef GBUFFER_DESCRIPTION_H
#define GBUFFER_DESCRIPTION_H

#include "Swapchain.h"
#include <stdexcept>

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>

// Must match the creation order in JungleApp.cpp
enum GBufferTarget {
    Albedo = 0,
    Depth = 1,
    Normal = 2,
    Motion = 3,
    Emission = 4,  //RGB, strength (factor 0..255)
    NumAttachments = 5,
};

inline VkFormat getGBufferAttachmentFormat(Swapchain *swapChain, GBufferTarget target) {
    switch (target) {
        case Albedo:
        case Emission:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case Normal:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case Motion:
            return VK_FORMAT_R32G32_SFLOAT;
        case Depth:
            return swapChain->chooseDepthFormat();
        default:
            throw std::runtime_error("Invalid attachment information requested!");
    }
}

#endif /* end of include guard: GBUFFER_DESCRIPTION_H */
