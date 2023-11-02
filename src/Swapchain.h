#ifndef JUNGLE_SWAPCHAIN
#define JUNGLE_SWAPCHAIN

#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "PhysicalDevice.h"

/**
 * A class which manages the swapchain and render targets for the window.
 */
class Swapchain
{
  public:
    Swapchain(GLFWwindow* window, VkSurfaceKHR surface, VulkanDevice* device);
    ~Swapchain();

    void createFramebuffersForRender(VkRenderPass renderPass);
    void recreateSwapChain(VkRenderPass renderPass);

    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    bool enableVSync = false;
    VkSwapchainKHR swapChain;
    std::vector<VkFramebuffer> swapChainFramebuffers;

  private:
    GLFWwindow *window;
    VulkanDevice *device;
    VkSurfaceKHR surface;

    void cleanupSwapChain();

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& availableFormats);

    VkPresentModeKHR chooseSwapPresentMode(
        const std::vector<VkPresentModeKHR>& availablePresentModes);

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;

    void createSwapChain();
    void createImageViews();
};

#endif /* end of include guard: JUNGLE_SWAPCHAIN */
