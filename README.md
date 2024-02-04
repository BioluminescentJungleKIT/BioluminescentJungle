# Bioluminescent Jungle

This project is a result of the graphics and game development course at KIT,
where 2-person teams implemented several real-time graphics techniques in 
their own project. Our project was mainly focused on rendering a mostly static 
scene in a nice way.

## Screenshots

![Fern, stones and Bromeliae near a river. They are reflected in it. The Bromeliae glow red in an eerie violet fog.](screenshots/screenshot_reflections.png)
![Broken down walls stand near a river. The image is scattered with butterflies glowing in different colors.](screenshots/screenshot_ruins.png)
![Glowing vines illuminate the ground near the trees they are growing on. The ray-traced light and shadows are smooth.](screenshots/screenshot_vines.png)

## Features

* Settings
* Scene loading
* Music loop playback
* Textures
* Instanced rendering
* Tone mapping
* Deferred shading
* Normal mapping
* First-person camera controller
  * Optional fixed height above ground
* Temporal antialiasing 
* Inverse displacement mapping
* Emissive fog based on https://ijdykeman.github.io/graphics/simple_fog_shader
* Screen space reflections
* GPU-driven level of detail with instancing
* Raytraced direct illumination using ReSTIR
  * Optimized using a grid architecture
* Butterflies (random movement)
* Procedural animations
  * Butterflies (wing flapping)
  * Wind
  * Water (moving texture)

## Building

Clone repository and libraries (`git clone --recurse-submodules https://github.com/BioluminescentJungleKIT/BioluminescentJungle.git`).

Install all required dependencies:
  * Vulkan
  * glm
  * glfw

Build using CMake.

## Usage

Run program in root folder (contains shaders/ and scene/). Syntax: `Jungle [PATH-TO-GLTF-FILE] [OPTIONS]`

Possible options are:

* `--hw-raytracing` use hardware-acceleration for raytracing
* `--recompile-shaders` recompile shaders on startup
* `--crash-on-validation-message` for debugging
* `--renderscale <FACTOR>` scale rendering resolution by `<FACTOR>`
* `--ratelimit <LIMIT>` limits FPS to `<LIMIT>`
* `--fullscreen` start in full screen mode


## License

While most code is our own, we incorporated some code from other sources.
Where we did, it is marked as such (to our best effort and except CC0 code from https://vulkan-tutorial.com).
We license our own code under MIT license and code from other sources under
their respective licenses (see NOTICE and comments). The assets we used in
our scene are self-made, CC0-licensed or AI-generated and we share the 
result under the CC BY-SA 4.0 license.