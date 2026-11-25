// SOFA HRTF loader: iplHRTFCreate with IPL_HRTFTYPE_SOFA and the input as the in-memory SOFA file
// (libmysofa HDF5 parse -> steam-audio resampling/normalization into its HRTF map), then a few
// frames of binaural rendering with the loaded HRTF. The first byte picks the normalization and
// interpolation; the rest is the SOFA file.
#include "fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1) return -1;
    uint8_t mode = data[0];
    IPLContext ctx = fuzz_context();
    if (!ctx) return 0;
    IPLAudioSettings audio{44100, 256};
    std::vector<IPLuint8> sofa(data + 1, data + size);
    IPLHRTFSettings hs{};
    hs.type = IPL_HRTFTYPE_SOFA;
    hs.sofaData = sofa.data();
    hs.sofaDataSize = (int)sofa.size();
    hs.volume = 1.0f;
    hs.normType = (mode & 1) ? IPL_HRTFNORMTYPE_RMS : IPL_HRTFNORMTYPE_NONE;
    IPLHRTF hrtf = nullptr;
    if (iplHRTFCreate(ctx, &audio, &hs, &hrtf) == IPL_STATUS_SUCCESS) {
        IPLBinauralEffectSettings es{hrtf};
        IPLBinauralEffect eff = nullptr;
        if (iplBinauralEffectCreate(ctx, &audio, &es, &eff) == IPL_STATUS_SUCCESS) {
            IPLAudioBuffer in{}, out{};
            iplAudioBufferAllocate(ctx, 1, audio.frameSize, &in);
            iplAudioBufferAllocate(ctx, 2, audio.frameSize, &out);
            for (int i = 0; i < audio.frameSize; ++i) in.data[0][i] = (i % 32) / 32.0f - 0.5f;
            const IPLVector3 dirs[] = {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}, {-0.6f, -0.6f, 0.53f}};
            for (const auto& d : dirs) {
                IPLBinauralEffectParams p{};
                p.direction = d;
                p.interpolation = (mode & 2) ? IPL_HRTFINTERPOLATION_BILINEAR : IPL_HRTFINTERPOLATION_NEAREST;
                p.spatialBlend = 1.0f;
                p.hrtf = hrtf;
                iplBinauralEffectApply(eff, &p, &in, &out);
            }
            iplAudioBufferFree(ctx, &in);
            iplAudioBufferFree(ctx, &out);
            iplBinauralEffectRelease(&eff);
        }
        iplHRTFRelease(&hrtf);
    }
    iplContextRelease(&ctx);
    return 0;
}
