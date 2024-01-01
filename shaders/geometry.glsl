
struct Triangle {
    vec4 x; // just xyz
    vec4 y; // just xyz
    vec4 z; // just xyz
};

struct Ray {
    vec3 origin;
    vec3 dir;
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
