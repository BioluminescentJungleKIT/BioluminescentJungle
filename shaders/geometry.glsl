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
    vec4 low; // just xyz
    vec4 high; // just xyz
    int left;
    int right;
};

// Thanks Stackoverflow
// https://stackoverflow.com/questions/59257678/intersect-a-ray-with-a-triangle-in-glsl-c
float PointInOrOn(vec3 P1, vec3 P2, vec3 A, vec3 B)
{
    vec3 CP1 = cross(B - A, P1 - A);
    vec3 CP2 = cross(B - A, P2 - A);
    return step(0.0, dot(CP1, CP2));
}

bool PointInTriangle(vec3 px, vec3 p0, vec3 p1, vec3 p2)
{
    return PointInOrOn(px, p0, p1, p2) * PointInOrOn(px, p1, p2, p0) * PointInOrOn(px, p2, p0, p1) > 0;
}

vec3 IntersectPlane(Ray ray, vec3 p0, vec3 p1, vec3 p2)
{
    vec3 N = cross(p1-p0, p2-p0);
    vec3 X = ray.origin + ray.dir * dot(p0 - ray.origin, N) / dot(ray.dir, N);
    return X;
}

float intersectTriangle(Triangle tri, Ray r) {
    vec3 X = IntersectPlane(r, tri.x.xyz, tri.y.xyz, tri.z.xyz);
    if (PointInTriangle(X, tri.x.xyz, tri.y.xyz, tri.z.xyz)) {
        return dot(X - r.origin, r.dir);
    }

    return -1.0;
}

// Adapted from: https://tavianator.com/2022/ray_box_boundary.html
vec2 intersectAABB(vec3 bounds[2], Ray r, vec2 tmimaxInit) {
    float tmin = tmimaxInit.x;
    float tmax = tmimaxInit.y;

    for (int d = 0; d < 3; ++d) {
        int sgn = int(sign(r.invDir[d]) < 0);
        float bmin = bounds[sgn][d];
        float bmax = bounds[1 - sgn][d];

        float dmin = (bmin - r.origin[d]) * r.invDir[d];
        float dmax = (bmax - r.origin[d]) * r.invDir[d];
        tmin = max(dmin, tmin);
        tmax = min(dmax, tmax);
    }

    return vec2(tmin, tmax);
}
