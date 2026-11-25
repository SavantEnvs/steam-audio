// Non-HRTF DSP effects: direct (distance attenuation / air absorption EQ / directivity /
// occlusion / transmission), panning, ambisonics encode / panning / rotation, and the
// parametric reflection effect. The input picks
// the effect, settings, per-frame parameters and the audio for several frames.
#include "fuzz_common.h"

static const int kRates[] = {24000, 44100, 48000};
static const int kFrames[] = {128, 256, 1024};

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
    FuzzReader r(data, size);
    IPLContext ctx = fuzz_context();
    if (!ctx) return 0;
    int which = r.pick(6);
    IPLAudioSettings audio{kRates[r.pick(3)], kFrames[r.pick(3)]};
    int frames = 1 + r.pick(4);
    IPLAudioBuffer in{}, out{};

    if (which == 0) {
        int ch = 1 + r.pick(4);
        IPLDirectEffectSettings s{ch};
        IPLDirectEffect e = nullptr;
        if (iplDirectEffectCreate(ctx, &audio, &s, &e) == IPL_STATUS_SUCCESS) {
            iplAudioBufferAllocate(ctx, ch, audio.frameSize, &in);
            iplAudioBufferAllocate(ctx, ch, audio.frameSize, &out);
            for (int f = 0; f < frames; ++f) {
                IPLDirectEffectParams p{};
                p.flags = (IPLDirectEffectFlags)(r.u8() & 0x1f);
                p.transmissionType = (IPLTransmissionType)r.pick(2);
                p.distanceAttenuation = r.rangef(0, 1);
                for (int b = 0; b < IPL_NUM_BANDS; ++b) p.airAbsorption[b] = r.rangef(0, 1);
                p.directivity = r.rangef(0, 1);
                p.occlusion = r.rangef(0, 1);
                for (int b = 0; b < IPL_NUM_BANDS; ++b) p.transmission[b] = r.rangef(0, 1);
                fill_audio(r, in);
                iplDirectEffectApply(e, &p, &in, &out);
            }
            iplDirectEffectRelease(&e);
        }
    } else if (which == 1) {
        std::vector<IPLVector3> spk;
        IPLPanningEffectSettings s{layout(r, spk)};
        IPLPanningEffect e = nullptr;
        if (iplPanningEffectCreate(ctx, &audio, &s, &e) == IPL_STATUS_SUCCESS) {
            iplAudioBufferAllocate(ctx, 1, audio.frameSize, &in);
            iplAudioBufferAllocate(ctx, layout_channels(s.speakerLayout), audio.frameSize, &out);
            for (int f = 0; f < frames; ++f) {
                IPLPanningEffectParams p{r.vec(1.0f)};
                fill_audio(r, in);
                iplPanningEffectApply(e, &p, &in, &out);
            }
            iplPanningEffectRelease(&e);
        }
    } else if (which == 2) {
        int maxOrder = r.pick(4);
        IPLAmbisonicsEncodeEffectSettings s{maxOrder};
        IPLAmbisonicsEncodeEffect e = nullptr;
        if (iplAmbisonicsEncodeEffectCreate(ctx, &audio, &s, &e) == IPL_STATUS_SUCCESS) {
            int order = r.pick(maxOrder + 1);
            iplAudioBufferAllocate(ctx, 1, audio.frameSize, &in);
            iplAudioBufferAllocate(ctx, (order + 1) * (order + 1), audio.frameSize, &out);
            for (int f = 0; f < frames; ++f) {
                IPLAmbisonicsEncodeEffectParams p{r.vec(1.0f), order};
                fill_audio(r, in);
                iplAmbisonicsEncodeEffectApply(e, &p, &in, &out);
            }
            iplAmbisonicsEncodeEffectRelease(&e);
        }
    } else if (which == 3) {
        int maxOrder = r.pick(4);
        std::vector<IPLVector3> spk;
        IPLAmbisonicsPanningEffectSettings s{layout(r, spk), maxOrder};
        IPLAmbisonicsPanningEffect e = nullptr;
        if (iplAmbisonicsPanningEffectCreate(ctx, &audio, &s, &e) == IPL_STATUS_SUCCESS) {
            int order = r.pick(maxOrder + 1);
            iplAudioBufferAllocate(ctx, (order + 1) * (order + 1), audio.frameSize, &in);
            iplAudioBufferAllocate(ctx, layout_channels(s.speakerLayout), audio.frameSize, &out);
            for (int f = 0; f < frames; ++f) {
                IPLAmbisonicsPanningEffectParams p{order};
                fill_audio(r, in);
                iplAmbisonicsPanningEffectApply(e, &p, &in, &out);
            }
            iplAmbisonicsPanningEffectRelease(&e);
        }
    } else if (which == 4) {
        int maxOrder = r.pick(4);
        IPLAmbisonicsRotationEffectSettings s{maxOrder};
        IPLAmbisonicsRotationEffect e = nullptr;
        if (iplAmbisonicsRotationEffectCreate(ctx, &audio, &s, &e) == IPL_STATUS_SUCCESS) {
            int order = r.pick(maxOrder + 1);
            iplAudioBufferAllocate(ctx, (order + 1) * (order + 1), audio.frameSize, &in);
            iplAudioBufferAllocate(ctx, (order + 1) * (order + 1), audio.frameSize, &out);
            for (int f = 0; f < frames; ++f) {
                IPLAmbisonicsRotationEffectParams p{};
                p.orientation = IPLCoordinateSpace3{r.vec(1), r.vec(1), r.vec(1), r.vec(10)};
                p.order = order;
                fill_audio(r, in);
                iplAmbisonicsRotationEffectApply(e, &p, &in, &out);
            }
            iplAmbisonicsRotationEffectRelease(&e);
        }
    } else {
        // Parametric reflection effect with caller-supplied reverb times, EQ and delay (the
        // convolution IR handle only comes from the simulator; see fuzz_simulator_reflections).
        IPLReflectionEffectType type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
        int order = r.pick(3);
        int ch = (order + 1) * (order + 1);
        int irSize = 1 + r.u16() % (audio.samplingRate / 2);
        IPLReflectionEffectSettings s{type, irSize, ch};
        IPLReflectionEffect e = nullptr;
        if (iplReflectionEffectCreate(ctx, &audio, &s, &e) == IPL_STATUS_SUCCESS) {
            iplAudioBufferAllocate(ctx, 1, audio.frameSize, &in);
            iplAudioBufferAllocate(ctx, ch, audio.frameSize, &out);
            IPLReflectionEffectParams p{};
            p.type = type;
            p.numChannels = ch;
            p.irSize = 1 + r.u16() % irSize;
            for (int b = 0; b < IPL_NUM_BANDS; ++b) { p.reverbTimes[b] = r.rangef(0.01f, 10); p.eq[b] = r.rangef(0, 1); }
            p.delay = r.u16() % (audio.samplingRate);
            for (int f = 0; f < frames; ++f) {
                fill_audio(r, in);
                iplReflectionEffectApply(e, &p, &in, &out, nullptr);
            }
            iplReflectionEffectGetTail(e, &out, nullptr);
            iplReflectionEffectRelease(&e);
        }
    }
    if (in.data) iplAudioBufferFree(ctx, &in);
    if (out.data) iplAudioBufferFree(ctx, &out);
    iplContextRelease(&ctx);
    return 0;
}
