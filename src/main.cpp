#include <iostream>
#include "JungleApp.h"
#include "PhysicalDevice.h"
#include "VulkanHelper.h"

int main(int argc, char **argv) {
    JungleApp app{};

    std::string scenePath = "scenes/big scene/big.gltf";
    if (argc > 1 && !std::string(argv[1]).starts_with("--")) {
        scenePath = argv[1];
    }

    bool recompileShaders = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--recompile-shaders")) {
            recompileShaders = true;
        }

        if (!strcmp(argv[i], "--crash-on-validation-message")) {
            crashOnValidationWarning = true;
        }

        if (!strcmp(argv[i], "--renderscale")) {
            Swapchain::renderScale = std::atof(argv[i+1]);
        }

        if (!strcmp(argv[i], "--ratelimit")) {
            Swapchain::rateLimit = std::atoi(argv[i+1]);
        }

        if (!strcmp(argv[i], "--hw-raytracing")) {
            useHWRaytracing = true;
        }

        if (!strcmp(argv[i], "--fullscreen")) {
            app.fullscreen = true;
        }
    }

    try {
        app.run(scenePath, recompileShaders);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
