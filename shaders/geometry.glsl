// Unless noted otherwise, content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.
//
// Parts of this file are licensed under MIT.
// Copyright (c) 2014 Andreas Reich, Johannes Jendersie.
//
// Parts of this file are licensed under MIT.
// Copyright (c) 2022 Tavian Barnes.

struct Triangle {
    vec4 x; // just xyz
    vec4 y; // just xyz
    vec4 z; // just xyz
};

struct Ray {
    vec3 origin;
    vec3 dir;
    vec3 invDir;
};

struct BVHNode {
    vec4 low; // just xyz, w is reinterpret_cast<float>(leftIdx)
    vec4 high; // just xyz, w is reinterpret_cast<float>(rightIdx)
};

// Thanks Stackoverflow
// https://stackoverflow.com/questions/54564286/triangle-intersection-test-in-opengl-es-glsl
// they actually copied it from https://github.com/Jojendersie/gpugi/blob/5d18526c864bbf09baca02bfab6bcec97b7e1210/gpugi/shader/intersectiontests.glsl#L63
//
// Beginning of section Copyright (c) 2014 Andreas Reich, Johannes Jendersie.
bool IntersectTriangle(Ray ray, vec3 p0, vec3 p1, vec3 p2,
        out float hit, out vec3 barycentricCoord, out vec3 triangleNormal)
{
    const vec3 e0 = p1 - p0;
    const vec3 e1 = p0 - p2;
    triangleNormal = cross( e1, e0 );

    const vec3 e2 = ( 1.0 / dot( triangleNormal, ray.dir ) ) * ( p0 - ray.origin );
    const vec3 i  = cross( ray.dir, e2 );

    barycentricCoord.y = dot( i, e1 );
    barycentricCoord.z = dot( i, e0 );
    barycentricCoord.x = 1.0 - (barycentricCoord.z + barycentricCoord.y);
    hit   = dot( triangleNormal, e2 );

    return  /*(hit < ray.tmax) && */ (hit > 0.001) && all(greaterThanEqual(barycentricCoord, vec3(0.0)));
}
// End of section Copyright (c) 2014 Andreas Reich, Johannes Jendersie.

float intersectTriangle(Triangle tri, Ray r) {
    float t;
    vec3 bcoord;
    vec3 normal;

    bool f = IntersectTriangle(r, tri.x.xyz, tri.y.xyz, tri.z.xyz, t, bcoord, normal);
    if (f) return t;
    return -1;
}

// Adapted from: https://tavianator.com/2022/ray_box_boundary.html
// Beginning of section Copyright (c) 2022 Tavian Barnes.
vec2 intersectAABB(vec3 bounds[2], Ray r, vec2 tmimaxInit) {
    float tmin = tmimaxInit.x;
    float tmax = tmimaxInit.y;

    vec3 t1 = (bounds[0] - r.origin) * r.invDir;
    vec3 t2 = (bounds[1] - r.origin) * r.invDir;

    float c1 = max(min(t1[0], t2[0]), max(min(t1[1], t2[1]), min(t1[2], t2[2])));
    float c2 = min(max(t1[0], t2[0]), min(max(t1[1], t2[1]), max(t1[2], t2[2])));

    tmin = max(c1, tmin);
    tmax = min(c2, tmax);

    return vec2(tmin, tmax);
}
// End of section Copyright (c) 2022 Tavian Barnes.
