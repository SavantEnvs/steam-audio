// Reflection + direct simulation over a room, then the matching effects. The input chooses the
// room size and materials, the simulator settings (reflection type CONVOLUTION/PARAMETRIC/HYBRID,
// IR duration, ambisonics order, rays, bounces), source/listener placement, and the hybrid reverb
// transition time and overlap (which may exceed the IR duration), then runs iplSimulatorRunDirect
// / iplSimulatorRunReflections and applies iplReflectionEffectApply / iplDirectEffectApply to
// audio from the input.
#include "fuzz_common.h"

static const IPLint32 kTris[12][3] = {
    {0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 4, 5}, {0, 5, 1},
    {3, 2, 6}, {3, 6, 7}, {0, 3, 7}, {0, 7, 4}, {1, 5, 6}, {1, 6, 2}};

static IPLCoordinateSpace3 frame_at(IPLVector3 o) {
    return IPLCoordinateSpace3{{1, 0, 0}, {0, 1, 0}, {0, 0, -1}, o};
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzReader r(data, size);
    IPLContext ctx = fuzz_context();
    if (!ctx) return 0;

    static const int kRates[] = {24000, 44100, 48000};
    static const int kFrames[] = {256, 512, 1024};
    IPLSimulationSettings ss{};
    ss.flags = (IPLSimulationFlags)(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    ss.sceneType = IPL_SCENETYPE_DEFAULT;
    ss.reflectionType = (IPLReflectionEffectType)r.pick(3);
    ss.maxNumOcclusionSamples = 16;
    ss.maxNumRays = 64 + r.pick(4) * 64;
    ss.numDiffuseSamples = 8 + r.pick(4) * 8;
    ss.maxDuration = r.rangef(0.05f, 1.0f);
    ss.maxOrder = r.pick(3);
    ss.maxNumSources = 1;
    ss.numThreads = 1;
    ss.samplingRate = kRates[r.pick(3)];
    ss.frameSize = kFrames[r.pick(3)];

    // Room: an axis-aligned box with input-chosen extents and materials.
    float hx = r.rangef(0.5f, 20.0f), hy = r.rangef(0.5f, 10.0f), hz = r.rangef(0.5f, 20.0f);
    IPLVector3 verts[8] = {{-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, hy, -hz}, {-hx, hy, -hz},
                           {-hx, -hy, hz},  {hx, -hy, hz},  {hx, hy, hz},  {-hx, hy, hz}};
    IPLTriangle tris[12];
    IPLint32 matIdx[12];
    IPLMaterial mats[2];
    for (auto& m : mats) {
        for (int b = 0; b < IPL_NUM_BANDS; ++b) m.absorption[b] = r.rangef(0, 1);
        m.scattering = r.rangef(0, 1);
        for (int b = 0; b < IPL_NUM_BANDS; ++b) m.transmission[b] = r.rangef(0, 1);
    }
    for (int i = 0; i < 12; ++i) {
        memcpy(tris[i].indices, kTris[i], sizeof(kTris[i]));
        matIdx[i] = r.pick(2);
    }

    IPLSimulationSharedInputs shared{};
    shared.listener = frame_at(r.vec(hx < hz ? hx : hz));
    shared.numRays = 1 + r.u16() % ss.maxNumRays;
    shared.numBounces = 1 + r.pick(16);
    shared.duration = r.rangef(0.01f, ss.maxDuration);
    shared.order = r.pick(ss.maxOrder + 1);
    shared.irradianceMinDistance = r.rangef(0.1f, 2.0f);

    IPLSimulationInputs in{};
    in.flags = ss.flags;
    in.directFlags = (IPLDirectSimulationFlags)(r.u8() & 0x1f);
    in.source = frame_at(r.vec(hx < hz ? hx : hz));
    in.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;
    in.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
    in.directivity.dipoleWeight = r.rangef(0, 1);
    in.directivity.dipolePower = r.rangef(0, 4);
    in.occlusionType = (IPLOcclusionType)r.pick(2);
    in.occlusionRadius = r.rangef(0.01f, 2.0f);
    in.numOcclusionSamples = 1 + r.pick(ss.maxNumOcclusionSamples);
    for (int b = 0; b < IPL_NUM_BANDS; ++b) in.reverbScale[b] = r.rangef(0, 2);
    in.hybridReverbTransitionTime = r.rangef(0.0f, 3.0f);
    in.hybridReverbOverlapPercent = r.rangef(0.0f, 1.0f);
    in.numTransmissionRays = 1 + r.pick(8);

    IPLSceneSettings scs{};
    scs.type = IPL_SCENETYPE_DEFAULT;
    IPLScene scene = nullptr;
    IPLStaticMesh mesh = nullptr;
    IPLSimulator sim = nullptr;
    IPLSource src = nullptr;
    if (iplSceneCreate(ctx, &scs, &scene) != IPL_STATUS_SUCCESS) goto done;
    {
        IPLStaticMeshSettings ms{8, 12, 2, verts, tris, matIdx, mats};
        if (iplStaticMeshCreate(scene, &ms, &mesh) != IPL_STATUS_SUCCESS) goto done;
        iplStaticMeshAdd(mesh, scene);
        iplSceneCommit(scene);
    }
    if (iplSimulatorCreate(ctx, &ss, &sim) != IPL_STATUS_SUCCESS) goto done;
    iplSimulatorSetScene(sim, scene);
    {
        IPLSourceSettings srcs{ss.flags};
        if (iplSourceCreate(sim, &srcs, &src) != IPL_STATUS_SUCCESS) goto done;
    }
    iplSourceAdd(src, sim);
    iplSimulatorCommit(sim);
    iplSourceSetInputs(src, ss.flags, &in);
    iplSimulatorSetSharedInputs(sim, ss.flags, &shared);
    iplSimulatorRunDirect(sim);
    iplSimulatorRunReflections(sim);
    {
        IPLSimulationOutputs out{};
        iplSourceGetOutputs(src, ss.flags, &out);

        IPLAudioSettings audio{ss.samplingRate, ss.frameSize};
        int numChannels = (shared.order + 1) * (shared.order + 1);
        int irSize = (int)ceilf(ss.maxDuration * ss.samplingRate);
        IPLReflectionEffectSettings rs{ss.reflectionType, irSize, numChannels};
        IPLReflectionEffect reff = nullptr;
        IPLDirectEffectSettings ds{1};
        IPLDirectEffect deff = nullptr;
        IPLAudioBuffer mono{}, amb{}, dout{};
        iplAudioBufferAllocate(ctx, 1, ss.frameSize, &mono);
        iplAudioBufferAllocate(ctx, numChannels, ss.frameSize, &amb);
        iplAudioBufferAllocate(ctx, 1, ss.frameSize, &dout);
        fill_audio(r, mono);
        if (iplReflectionEffectCreate(ctx, &audio, &rs, &reff) == IPL_STATUS_SUCCESS) {
            out.reflections.type = ss.reflectionType;
            out.reflections.numChannels = numChannels;
            out.reflections.irSize = (int)ceilf(shared.duration * ss.samplingRate);
            for (int f = 0; f < 3; ++f) iplReflectionEffectApply(reff, &out.reflections, &mono, &amb, nullptr);
            iplReflectionEffectGetTail(reff, &amb, nullptr);
            iplReflectionEffectRelease(&reff);
        }
        if (iplDirectEffectCreate(ctx, &audio, &ds, &deff) == IPL_STATUS_SUCCESS) {
            out.direct.flags = (IPLDirectEffectFlags)(r.u8() & 0x1f);
            out.direct.transmissionType = (IPLTransmissionType)r.pick(2);
            iplDirectEffectApply(deff, &out.direct, &mono, &dout);
            iplDirectEffectRelease(&deff);
        }
        iplAudioBufferFree(ctx, &mono);
        iplAudioBufferFree(ctx, &amb);
        iplAudioBufferFree(ctx, &dout);
    }
done:
    if (src) { iplSourceRemove(src, sim); iplSimulatorCommit(sim); iplSourceRelease(&src); }
    if (sim) iplSimulatorRelease(&sim);
    if (mesh) iplStaticMeshRelease(&mesh);
    if (scene) iplSceneRelease(&scene);
    iplContextRelease(&ctx);
    return 0;
}
