#include "util.glsl"

// NUM_SAMPLES_PER_RESERVOIR must also be adjusted in Lighting.cpp.
// The value there must be >= than the value used by the shader, otherwise we don't have enough storage space.
#define NUM_SAMPLES_PER_RESERVOIR 1

// Very simple pseudo-random number generators.
// We might want something better, I just used this as a easy start
uint hash(uint x) {
    x += (x << 10u);
    x ^= (x >>  6u);
    x += (x <<  3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

uint getRandSeed(uvec3 state) {
    return hash(state.x) ^ hash(state.y * 23999 + state.z);
}

uint nextRand16bit(inout uint randState) {
    // Random prime numbers
    randState = randState * 1100003563 + 132233;
	return randState >> 16;
}

float nextRand(inout uint randState) {
    return nextRand16bit(randState) / 65535.0f;
}

vec2 nextRandV2(inout uint randState) {
    return vec2(nextRand(randState), nextRand(randState));
}

struct Reservoir {
    // 0 => invalid, > 0: point light number+1, < 0: negated (emissive triangle id+1)
    int selected[NUM_SAMPLES_PER_RESERVOIR];

    // Position of (or on) the selected light source
    vec3 position[NUM_SAMPLES_PER_RESERVOIR];

    // Sum of sampling weights
    float sumW[NUM_SAMPLES_PER_RESERVOIR];

    // The estimated light contribution
    float pHat[NUM_SAMPLES_PER_RESERVOIR];

    int totalNumSamples;
};

Reservoir createEmptyReservoir() {
    Reservoir r;
    for (int i = 0; i < NUM_SAMPLES_PER_RESERVOIR; i++) {
        r.selected[i] = 0;
        r.sumW[i] = 0;
        r.pHat[i] = 0;
        r.position[i] = vec3(0);
    }

    r.totalNumSamples = 0;
    return r;
}

void updateReservoirIdx(inout Reservoir r, inout uint randState, int id, float w, float pHat, vec3 pos, int i) {
    r.sumW[i] += w;
    if (w > 0 && nextRand(randState) < w / r.sumW[i]) {
        r.selected[i] = id;
        r.pHat[i] = pHat;
        r.position[i] = pos;
    }
}

void addSample(inout Reservoir r, inout uint randState, int id, float w, float pHat, vec3 pos) {
    r.totalNumSamples += 1;
    for (int i = 0; i < NUM_SAMPLES_PER_RESERVOIR; i++) {
        updateReservoirIdx(r, randState, id, w, pHat, pos, i);
    }
}

void mergeReservoir(inout uint randState, inout Reservoir dest, Reservoir src, float[NUM_SAMPLES_PER_RESERVOIR] correctedPHats,
        float maxSamplesTake)
{
    if (src.totalNumSamples <= 0) {
        return;
    }

    // Correction factor to make sure that the old samples don't outweigh the new ones too much.
    const float f = min(maxSamplesTake, src.totalNumSamples) * (1.0 / src.totalNumSamples);
    for (int i = 0; i < NUM_SAMPLES_PER_RESERVOIR; i++) {
        if (src.selected[i] != 0 && src.pHat[i] > 0) {
            float w = correctedPHats[i] / src.pHat[i] * src.sumW[i] * f;
            updateReservoirIdx(dest, randState, src.selected[i], w, correctedPHats[i], src.position[i], i);
        }
    }

    dest.totalNumSamples += int(src.totalNumSamples * f);
}

int reservoirIdx(ivec2 pos, int viewportWidth) {
    return pos.y * viewportWidth + pos.x;
}

float sum3(vec3 x) {
    return x.x + x.y + x.z;
}

void getEmissiveTriangleParams(EmissiveTriangle tri, out float intensity, out vec3 N, out float area) {
    intensity = tri.emission.w;
    vec3 p1 = tri.x.xyz;
    vec3 p2 = tri.y.xyz;
    vec3 p3 = tri.z.xyz;
    N = cross(p2 - p1, p3 - p1);
    area = length(N);
    N /= area;
}

// The following few functions are declared / duplicated in the respective shaders because it seems I can't declare the SSBOs in the included file,
// and I cannot pass a variable length array as a parameter. https://github.com/KhronosGroup/GLSL/issues/142
PointLightParams getPointLight(int idx);
void getEmissiveTriangle(int idx, out float intensity, out vec3 N, out float area);

void calculatePHats(Reservoir r, SurfacePoint point, out float pHats[NUM_SAMPLES_PER_RESERVOIR],
        in SceneLightInfo slInfo) {
    for (int i = 0; i < NUM_SAMPLES_PER_RESERVOIR; i++) {
        int sel = r.selected[i];
        if (sel < 0) {
            vec3 N;
            float intensity, area;
            getEmissiveTriangle(-sel-1, intensity, N, area);
            pHats[i] = evalEmittingPoint(point, r.position[i], N) * intensity;
        } else if (sel > 0) {
            pHats[i] = evalPointLightStrength(point, getPointLight(sel-1));
            pHats[i] *= slInfo.pointLightIntensityMultiplier;
        } else {
            pHats[i] = 0;
        }
    }
}

void tryMergeReservoir(inout uint randState, inout Reservoir dest, Reservoir src, float maxSamplesTake,
        in SceneLightInfo slInfo, SurfacePoint point)
{
    if (src.totalNumSamples <= 0) {
        return;
    }

    float pHats[NUM_SAMPLES_PER_RESERVOIR];
    calculatePHats(src, point, pHats, slInfo);
    mergeReservoir(randState, dest, src, pHats, maxSamplesTake);
}
