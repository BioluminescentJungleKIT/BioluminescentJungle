# Bioluminescent Jungle
Copyright (c) 2024 Ilia Bozhinov, Lars Erber

Where not noted otherwise, all code in shaders/ and src/ is licensed under MIT.
The scene in scene/big scene/ is licensed under CC BY-SA 4.0.


## Portions licensed under MIT from other authors

The sampleCatmullRom function in shaders/taa.frag was adapted from MJP's implementation (https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1).
Copyright (c) 2019 MJP.

The hable tonemapping function in shaders/tonemap.frag was adapted from Matt Taylor's implementation (https://64.github.io/tonemapping/).
Copyright (c) 2022 @64.

The intersectAABB function in shaders/geometry.glsl was adapted from Tavian Barnes' implementation (https://tavianator.com/2022/ray_box_boundary.html).
Copyright (c) 2022 Tavian Barnes.

The intersectAABB function in src/BVH.hpp was adapted from Tavian Barnes' implementation (https://tavianator.com/2022/ray_box_boundary.html).
Copyright (c) 2022 Tavian Barnes.

The file shaders/noise3D.glsl was developed by Ashima Arts and Stefan Gustavson (https://github.com/ashima/webgl-noise, https://github.com/stegu/webgl-noise).
Copyright (C) 2011 by Ashima Arts (Simplex noise).
Copyright (C) 2011-2016 by Stefan Gustavson (Classic noise and others).

The file src/Raytracing.hpp was adapted from Sasha Willems' implementation (https://github.com/SaschaWillems/Vulkan/blob/master/examples/rayquery/rayquery.cpp).
Copyright (C) 2020-2023 by Sascha Willems - www.saschawillems.de.

The IntersectTriangle function in shaders/geometry.glsl was taken from Andreas Reich and Johannes Jendersie's inplementation (https://github.com/Jojendersie/gpugi/blob/5d18526c864bbf09baca02bfab6bcec97b7e1210/gpugi/shader/intersectiontests.glsl).
Copyright (c) 2014 Andreas Reich, Johannes Jendersie.

The intersectTriangle function in src/BVH.hpp was adapted from Andreas Reich and Johannes Jendersie's inplementation (https://github.com/Jojendersie/gpugi/blob/5d18526c864bbf09baca02bfab6bcec97b7e1210/gpugi/shader/intersectiontests.glsl).
Copyright (c) 2014 Andreas Reich, Johannes Jendersie.

## Portions licensed under Apache 2.0 from other authors
The closestPointTriangle function in shaders/butterflies.comp was adapted from Intel Corporation's implementation (https://github.com/embree/embree/blob/master/tutorials/common/math/closest_point.h).
Copyright 2009-2021 Intel Corporation.

## Acknowledgement of use of resources licensed under CC0
This project was created using the following CC0 resources:
* https://vulkan-tutorial.com/
* https://ambientcg.com/view?id=Foliage001
* https://pixnio.com/textures-and-patterns/leaves-plants-patterns-textures/leaf-leaves-branch-acacia
* https://polyhaven.com/a/aerial_grass_rock
* https://ambientcg.com/view?id=Rock051
* https://polyhaven.com/a/rough_block_wall
* https://ambientcg.com/view?id=Bark004
