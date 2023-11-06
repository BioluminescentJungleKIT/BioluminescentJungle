#include <iostream>
#include "JungleApp.h"

int main(int argc, char **argv) {
    JungleApp app{};

    std::string scenePath = "scenes/apple/food_apple_01_4k.gltf";
    if (argc > 1) {
        scenePath = argv[1];
    }

    try {
        app.run(scenePath);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
