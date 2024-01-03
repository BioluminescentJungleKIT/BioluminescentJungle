#define NUM_SAMPLES_PER_RESERVOIR 3
#define NUM_SAMPLES_PER_PIXEL 10

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

    int totalNumSamples;
};

Reservoir createEmptyReservoir() {
    Reservoir r;
    for (int i = 0; i < NUM_SAMPLES_PER_RESERVOIR; i++) {
        r.selected[i] = -1;
        r.sumW[i] = 0;
    }

    r.totalNumSamples = 0;
    return r;
}

void addSample(inout Reservoir r, inout uint randState, int id, float w) {
    for (int i = 0; i < NUM_SAMPLES_PER_RESERVOIR; i++) {
        r.sumW[i] += w;
        if (nextRand(randState) < w / r.sumW[i]) {
            r.selected[i] = id;
        }
    }

    r.totalNumSamples += 1;
}

int reservoirIdx(ivec2 pos, int viewportWidth) {
    return pos.y * viewportWidth + pos.x;
}
