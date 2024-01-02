#include <iostream>
#include "JungleApp.h"
#include "PhysicalDevice.h"

int main(int argc, char **argv) {
    JungleApp app{};

    std::string scenePath = "scenes/apple/food_apple_01_4k.gltf";
    if (argc > 1) {
        scenePath = argv[1];
    }

    bool recompileShaders = false;

    for (int i = 2; i < argc; i++) {
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
    }

    try {
        app.run(scenePath, recompileShaders);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
