#include "util.glsl"

// NUM_SAMPLES_PER_RESERVOIR must also be adjusted in Lighting.cpp.
// The value there must be >= than the value used by the shader, otherwise we don't have enough storage space.
#define NUM_SAMPLES_PER_RESERVOIR 1
#define NUM_SAMPLES_PER_PIXEL 16

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

float nextRand(inout uint randState) {
    // Random prime numbers
    randState = randState * 1100003563 + 132233;
	return (randState >> 16) / 65535.0f;
}

struct Reservoir {
    // Sample == light index
    int selected[NUM_SAMPLES_PER_RESERVOIR];

    // Sum of sampling weights
    float sumW[NUM_SAMPLES_PER_RESERVOIR];

    // The corrected W as in Equation (6) from the paper
    float pHat[NUM_SAMPLES_PER_RESERVOIR];

    int totalNumSamples;
};

Reservoir createEmptyReservoir() {
    Reservoir r;
    for (int i = 0; i < NUM_SAMPLES_PER_RESERVOIR; i++) {
        r.selected[i] = -1;
        r.sumW[i] = 0;
        r.pHat[i] = 0;
    }

    r.totalNumSamples = 0;
    return r;
}

void updateReservoirIdx(inout Reservoir r, inout uint randState, int id, float w, float pHat, int i) {
    r.sumW[i] += w;
    if (nextRand(randState) < w / r.sumW[i]) {
        r.selected[i] = id;
        r.pHat[i] = pHat;
    }
}

void addSample(inout Reservoir r, inout uint randState, int id, float w, float pHat) {
    r.totalNumSamples += 1;
    for (int i = 0; i < NUM_SAMPLES_PER_RESERVOIR; i++) {
        updateReservoirIdx(r, randState, id, w, pHat, i);
    }
}

void mergeReservoir(inout uint randState, inout Reservoir dest, Reservoir src, float[NUM_SAMPLES_PER_RESERVOIR] correctedPHats, float maxPC) {
    if (src.totalNumSamples <= 0) {
        return;
    }

    // Correction factor to make sure that the old samples don't outweigh the new ones too much.
    const float f = min(dest.totalNumSamples * maxPC, src.totalNumSamples) / src.totalNumSamples;
    for (int i = 0; i < NUM_SAMPLES_PER_RESERVOIR; i++) {
        if (src.selected[i] >= 0) {
            float w = correctedPHats[i] / src.pHat[i] * src.sumW[i] * f;
            updateReservoirIdx(dest, randState, src.selected[i], w, correctedPHats[i], i);
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

float calcPHat(SurfacePoint point, PointLightParams light, SceneLightInfo slInfo, float d) {
    vec4 fog = evalFog(point, light, slInfo);
    if (d > 0.999) {
        // Background => need to estimate fog only
        return sum3(fog.rgb);
    } else {
        // Estimate diffuse illumination of a surface point
        return evalPointLightStrength(point, light) + sum3(fog.rgb);
    }
}

float calcPHatPartial(vec4 fog, float f, float d) {
    if (d > 0.999) {
        // Background => need to estimate fog only
        return sum3(fog.rgb);
    } else {
        // Estimate diffuse illumination of a surface point
        return f + sum3(fog.rgb);
    }
}
