// HRTF-based rendering with the built-in HRTF: binaural, ambisonics binaural, ambisonics decode
// (binaural) and virtual surround effects. The input picks the effect, sampling rate / frame size,
// directions, interpolation, spatial blend, ambisonics order, speaker layout (including custom
// speaker positions) and the audio samples for several frames.
//
// State kept across inputs on purpose: one context and one default HRTF per audio setting, created
// once and only read by the effects (the HRTF is immutable after creation).
#include "fuzz_common.h"

static const int kRates[] = {24000, 44100, 48000};
static const int kFrames[] = {128, 256, 1024};
static IPLContext gCtx;
static IPLHRTF gHrtf[3][3];

static IPLHRTF default_hrtf(int ri, int fi) {
    if (!gHrtf[ri][fi]) {
        IPLAudioSettings a{kRates[ri], kFrames[fi]};
        IPLHRTFSettings hs{};
        hs.type = IPL_HRTFTYPE_DEFAULT;
        hs.volume = 1.0f;
        iplHRTFCreate(gCtx, &a, &hs, &gHrtf[ri][fi]);
    }
    return gHrtf[ri][fi];
}

static IPLSpeakerLayout layout(FuzzReader& r, std::vector<IPLVector3>& speakers) {
    IPLSpeakerLayout l{};
    l.type = (IPLSpeakerLayoutType)r.pick(6);
    if (l.type == IPL_SPEAKERLAYOUTTYPE_CUSTOM) {
        speakers.resize(1 + r.pick(16));
        for (auto& s : speakers) s = r.vec(1.0f);
        l.numSpeakers = (int)speakers.size();
        l.speakers = speakers.data();
    }
    return l;
}

static int layout_channels(const IPLSpeakerLayout& l) {
    static const int n[] = {1, 2, 4, 6, 8};
    return l.type == IPL_SPEAKERLAYOUTTYPE_CUSTOM ? l.numSpeakers : n[l.type];
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!gCtx && !(gCtx = fuzz_context())) return 0;
    FuzzReader r(data, size);
    int which = r.pick(4);
    int ri = r.pick(3), fi = r.pick(3);
    IPLAudioSettings audio{kRates[ri], kFrames[fi]};
    IPLHRTF hrtf = default_hrtf(ri, fi);
    if (!hrtf) return 0;
    int frames = 1 + r.pick(4);
    IPLAudioBuffer in{}, out{};
    iplAudioBufferAllocate(gCtx, 2, audio.frameSize, &out);

    if (which == 0) {
        IPLBinauralEffectSettings s{hrtf};
        IPLBinauralEffect e = nullptr;
        if (iplBinauralEffectCreate(gCtx, &audio, &s, &e) == IPL_STATUS_SUCCESS) {
            int ch = 1 + r.pick(2);
            iplAudioBufferAllocate(gCtx, ch, audio.frameSize, &in);
            for (int f = 0; f < frames; ++f) {
                IPLBinauralEffectParams p{};
                p.direction = r.vec(1.0f);
                p.interpolation = (IPLHRTFInterpolation)r.pick(2);
                p.spatialBlend = r.rangef(0, 1);
                p.hrtf = hrtf;
                float peaks[2];
                p.peakDelays = (r.u8() & 1) ? peaks : nullptr;
                fill_audio(r, in);
                iplBinauralEffectApply(e, &p, &in, &out);
            }
            iplBinauralEffectGetTail(e, &out);
            iplBinauralEffectRelease(&e);
        }
    } else if (which == 1) {
        int maxOrder = r.pick(4);
        IPLAmbisonicsBinauralEffectSettings s{hrtf, maxOrder};
        IPLAmbisonicsBinauralEffect e = nullptr;
        if (iplAmbisonicsBinauralEffectCreate(gCtx, &audio, &s, &e) == IPL_STATUS_SUCCESS) {
            int order = r.pick(maxOrder + 1);
            iplAudioBufferAllocate(gCtx, (order + 1) * (order + 1), audio.frameSize, &in);
            for (int f = 0; f < frames; ++f) {
                IPLAmbisonicsBinauralEffectParams p{hrtf, order};
                fill_audio(r, in);
                iplAmbisonicsBinauralEffectApply(e, &p, &in, &out);
            }
            iplAmbisonicsBinauralEffectGetTail(e, &out);
            iplAmbisonicsBinauralEffectRelease(&e);
        }
    } else if (which == 2) {
        int maxOrder = r.pick(4);
        std::vector<IPLVector3> spk;
        IPLAmbisonicsDecodeEffectSettings s{layout(r, spk), hrtf, maxOrder};
        IPLAmbisonicsDecodeEffect e = nullptr;
        if (iplAmbisonicsDecodeEffectCreate(gCtx, &audio, &s, &e) == IPL_STATUS_SUCCESS) {
            // phonon.h allows order < maxOrder here, but AmbisonicsDecodeEffect rotates into a
            // buffer sized for maxOrder and AmbisonicsRotateEffect::apply asserts on the channel
            // count, so any order < maxOrder aborts at once. That known shallow abort hid every
            // other path of this target, so the decode effect always runs at maxOrder.
            int order = maxOrder;
            IPLbool binaural = (r.u8() & 1) ? IPL_TRUE : IPL_FALSE;
            IPLAudioBuffer dout{};
            // the effect only renders binaurally for a stereo layout; otherwise it pans to the layout
            bool toBinaural = binaural && s.speakerLayout.type == IPL_SPEAKERLAYOUTTYPE_STEREO;
            iplAudioBufferAllocate(gCtx, toBinaural ? 2 : layout_channels(s.speakerLayout), audio.frameSize, &dout);
            iplAudioBufferAllocate(gCtx, (order + 1) * (order + 1), audio.frameSize, &in);
            for (int f = 0; f < frames; ++f) {
                IPLAmbisonicsDecodeEffectParams p{};
                p.order = order;
                p.hrtf = hrtf;
                p.orientation = IPLCoordinateSpace3{{1, 0, 0}, {0, 1, 0}, {0, 0, -1}, r.vec(10)};
                p.binaural = binaural;
                fill_audio(r, in);
                iplAmbisonicsDecodeEffectApply(e, &p, &in, &dout);
            }
            iplAudioBufferFree(gCtx, &dout);
            iplAmbisonicsDecodeEffectRelease(&e);
        }
    } else {
        std::vector<IPLVector3> spk;
        IPLVirtualSurroundEffectSettings s{layout(r, spk), hrtf};
        IPLVirtualSurroundEffect e = nullptr;
        if (iplVirtualSurroundEffectCreate(gCtx, &audio, &s, &e) == IPL_STATUS_SUCCESS) {
            iplAudioBufferAllocate(gCtx, layout_channels(s.speakerLayout), audio.frameSize, &in);
            for (int f = 0; f < frames; ++f) {
                IPLVirtualSurroundEffectParams p{hrtf};
                fill_audio(r, in);
                iplVirtualSurroundEffectApply(e, &p, &in, &out);
            }
            iplVirtualSurroundEffectGetTail(e, &out);
            iplVirtualSurroundEffectRelease(&e);
        }
    }
    if (in.data) iplAudioBufferFree(gCtx, &in);
    iplAudioBufferFree(gCtx, &out);
    return 0;
}
