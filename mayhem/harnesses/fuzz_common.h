// Shared helpers for the steam-audio harnesses: a byte reader that turns fuzz input into bounded
// parameters, and context creation through the public API (phonon.h).
#pragma once
#include <phonon.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <vector>

struct FuzzReader {
    const uint8_t* p; size_t n;
    FuzzReader(const uint8_t* d, size_t s) : p(d), n(s) {}
    uint8_t u8() { if (!n) return 0; uint8_t v = *p; ++p; --n; return v; }
    uint16_t u16() { return (uint16_t)(u8() | (u8() << 8)); }
    uint32_t u32() { return (uint32_t)u16() | ((uint32_t)u16() << 16); }
    // any float the input encodes (including NaN/Inf): the API takes these from callers as-is
    float rawf() { uint32_t v = u32(); float f; memcpy(&f, &v, 4); return f; }
    // a float in [lo, hi]
    float rangef(float lo, float hi) { return lo + (hi - lo) * (u16() / 65535.0f); }
    int pick(int count) { return count ? u8() % count : 0; }
    IPLVector3 vec(float lim) { return IPLVector3{rangef(-lim, lim), rangef(-lim, lim), rangef(-lim, lim)}; }
};

static inline IPLContext fuzz_context() {
    IPLContextSettings s{};
    s.version = STEAMAUDIO_VERSION;
    s.simdLevel = IPL_SIMDLEVEL_SSE2;
    IPLContext ctx = nullptr;
    if (iplContextCreate(&s, &ctx) != IPL_STATUS_SUCCESS) return nullptr;
    return ctx;
}

// Fills an audio buffer with samples in [-1, 1] taken from the input (zeros once it runs out).
static inline void fill_audio(FuzzReader& r, IPLAudioBuffer& b) {
    for (int c = 0; c < b.numChannels; ++c)
        for (int i = 0; i < b.numSamples; ++i)
            b.data[c][i] = r.n ? r.rangef(-1.0f, 1.0f) : 0.0f;
}
