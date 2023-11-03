#ifndef PHYSICAL_DEVICE_H
#define PHYSICAL_DEVICE_H

#define GLFW_INCLUDE_VULKAN

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>

/**
 * The physical device class is meant to encapsulate all device-specific functions: allocating memory/images,
 * choosing best formats, checking support for features, managing queues. Also manages the VkInstance.
 */
class VulkanDevice
{
  public:
    VkInstance instance;

    void initInstance();
    void initDeviceForSurface(VkSurfaceKHR surface);

    void destroy();

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;

    // Allow implicit conversion to a VkDevice, makes our life much easier
    operator VkDevice() {
        return device;
    }

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() {
            return graphicsFamily.has_value() && presentFamily.has_value();
        };
    };

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);

    void createImage(uint32_t width, uint32_t height, VkFormat format,
        VkImageTiling tiling, VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties, VkImage& image,
        VkDeviceMemory& imageMemory);

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    VkCommandBuffer beginSingleTimeCommands(VkCommandPool commandPool);
    void endSingleTimeCommands(VkCommandPool commandPool, VkCommandBuffer commandBuffer);

    void transitionImageLayout(VkCommandPool pool, VkImage image, VkFormat format,
        VkImageLayout oldLayout, VkImageLayout newLayout);

  private:
    static bool checkValidationLayerSupport();
    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pDebugMessenger);

    void DestroyDebugUtilsMessengerEXT(VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        const VkAllocationCallbacks* pAllocator);

    VkDebugUtilsMessengerEXT debugMessenger;
    void createInstance();

    bool checkDeviceExtensionSupport(VkPhysicalDevice const& device);

    void setupDebugMessenger();
    void pickPhysicalDevice(VkSurfaceKHR surface);
    bool isDeviceSuitable(VkPhysicalDevice const& device, VkSurfaceKHR surface);
    void createLogicalDevice(VkSurfaceKHR surface);
};


#endif /* end of include guard: PHYSICAL_DEVICE_H */
