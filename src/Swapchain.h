#ifndef JUNGLE_SWAPCHAIN
#define JUNGLE_SWAPCHAIN

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "PhysicalDevice.h"

const int MAX_FRAMES_IN_FLIGHT = 2;

/**
 * A class to maintain an off-screen collection of render targets, one per swapchain frame
 */
class RenderTarget {
  public:
    void init(VulkanDevice* device, int nrFrames);
    void destroyAll();

    void addAttachment(std::vector<VkImage> imagePerFrame, VkFormat fmt, VkImageAspectFlags aspectFlags);
    void addAttachment(VkExtent2D extent, VkFormat fmt,
        VkImageUsageFlags usageFlags, VkImageAspectFlags aspectFlags,
        std::optional<VkImageAspectFlags> sampleFrom = {});
    void createFramebuffers(VkRenderPass renderPass, VkExtent2D extent);
    std::vector<VkFramebuffer> framebuffers;

    // A list of image views attached to the corresponding framebuffer
    std::vector<std::vector<VkImageView>> imageViews;
    std::vector<std::vector<VkImage>> images;

  private:
    VulkanDevice *device;
    int nrFrames;
    std::vector<VkDeviceMemory> deviceMemories;

    int framesInFlight;
};

/**
 * A class which manages the swapchain and render targets for the window.
 */
class Swapchain
{
  public:
    Swapchain(GLFWwindow* window, VkSurfaceKHR surface, VulkanDevice* device);
    ~Swapchain();

    std::optional<uint32_t> acquireNextImage(VkRenderPass renderPass);
    VkResult queuePresent(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    void createFramebuffersForRender(VkRenderPass renderPass);
    void recreateSwapChain(VkRenderPass renderPass);

    VkFormat swapChainImageFormat;

    VkExtent2D finalBufferSize;
    static float renderScale;

    VkExtent2D renderSize() {
        return {
            (uint32_t)(finalBufferSize.width * renderScale),
            (uint32_t)(finalBufferSize.height * renderScale),
        };
    }

    bool enableVSync = false;
    VkSwapchainKHR swapChain;

    RenderTarget defaultTarget;
    VkFormat chooseDepthFormat();

    uint32_t currentFrame = 0;

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

    void createSyncObjects();
    void createSwapChain();
    void createImageViews();

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
};

#endif /* end of include guard: JUNGLE_SWAPCHAIN */
