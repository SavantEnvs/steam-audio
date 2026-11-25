// Direct (occlusion / transmission ray casts) and pathing simulation over input-built geometry.
// The input supplies up to 32 triangles with materials, up to 8 probes, path-bake parameters
// (iplPathBakerBake: probe visibility + path finding), the source/listener placement and direct
// simulation flags; then iplSimulatorRunDirect / iplSimulatorRunPathing run and the pathing output
// is rendered with iplPathEffectApply.
#include "fuzz_common.h"

static IPLCoordinateSpace3 frame_at(IPLVector3 o) {
    return IPLCoordinateSpace3{{1, 0, 0}, {0, 1, 0}, {0, 0, -1}, o};
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FuzzReader r(data, size);
    IPLContext ctx = fuzz_context();
    if (!ctx) return 0;

    int numTris = 1 + r.pick(32);
    int numVerts = 3 + r.pick(3 * numTris - 2);
    std::vector<IPLVector3> verts(numVerts);
    for (auto& v : verts) v = r.vec(20.0f);
    std::vector<IPLTriangle> tris(numTris);
    std::vector<IPLint32> matIdx(numTris);
    IPLMaterial mats[2];
    for (auto& m : mats) {
        for (int b = 0; b < IPL_NUM_BANDS; ++b) m.absorption[b] = r.rangef(0, 1);
        m.scattering = r.rangef(0, 1);
        for (int b = 0; b < IPL_NUM_BANDS; ++b) m.transmission[b] = r.rangef(0, 1);
    }
    for (int i = 0; i < numTris; ++i) {
        for (int k = 0; k < 3; ++k) tris[i].indices[k] = r.pick(numVerts);
        matIdx[i] = r.pick(2);
    }

    IPLSceneSettings scs{};
    scs.type = IPL_SCENETYPE_DEFAULT;
    IPLScene scene = nullptr;
    IPLStaticMesh mesh = nullptr;
    IPLProbeBatch batch = nullptr;
    IPLSimulator sim = nullptr;
    IPLSource src = nullptr;
    IPLSimulationFlags flags = (IPLSimulationFlags)(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_PATHING);
    IPLBakedDataIdentifier id{};
    id.type = IPL_BAKEDDATATYPE_PATHING;
    id.variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
    IPLSimulationSettings ss{};
    IPLSimulationSharedInputs shared{};
    IPLSimulationInputs in{};

    if (iplSceneCreate(ctx, &scs, &scene) != IPL_STATUS_SUCCESS) goto done;
    {
        IPLStaticMeshSettings ms{numVerts, numTris, 2, verts.data(), tris.data(), matIdx.data(), mats};
        if (iplStaticMeshCreate(scene, &ms, &mesh) != IPL_STATUS_SUCCESS) goto done;
        iplStaticMeshAdd(mesh, scene);
        iplSceneCommit(scene);
    }
    if (iplProbeBatchCreate(ctx, &batch) != IPL_STATUS_SUCCESS) goto done;
    {
        int numProbes = 1 + r.pick(8);
        for (int i = 0; i < numProbes; ++i) iplProbeBatchAddProbe(batch, IPLSphere{r.vec(20.0f), r.rangef(0.1f, 10.0f)});
        iplProbeBatchCommit(batch);
        IPLPathBakeParams pb{};
        pb.scene = scene;
        pb.probeBatch = batch;
        pb.identifier = id;
        pb.numSamples = 1 + r.pick(16);
        pb.radius = r.rangef(0.01f, 2.0f);
        pb.threshold = r.rangef(0, 1);
        pb.visRange = r.rangef(0.1f, 50.0f);
        pb.pathRange = r.rangef(0.1f, 100.0f);
        pb.numThreads = 1;
        iplPathBakerBake(ctx, &pb, nullptr, nullptr);
    }

    ss.flags = flags;
    ss.sceneType = IPL_SCENETYPE_DEFAULT;
    ss.maxNumOcclusionSamples = 16;
    ss.maxOrder = r.pick(3);
    ss.maxNumSources = 1;
    ss.numThreads = 1;
    ss.numVisSamples = 1 + r.pick(16);
    ss.samplingRate = 48000;
    ss.frameSize = 256;
    if (iplSimulatorCreate(ctx, &ss, &sim) != IPL_STATUS_SUCCESS) goto done;
    iplSimulatorSetScene(sim, scene);
    iplSimulatorAddProbeBatch(sim, batch);
    {
        IPLSourceSettings srcs{flags};
        if (iplSourceCreate(sim, &srcs, &src) != IPL_STATUS_SUCCESS) goto done;
    }
    iplSourceAdd(src, sim);
    iplSimulatorCommit(sim);

    shared.listener = frame_at(r.vec(20.0f));
    shared.order = ss.maxOrder;
    in.flags = flags;
    in.directFlags = (IPLDirectSimulationFlags)(r.u8() & 0x1f);
    in.source = frame_at(r.vec(20.0f));
    in.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;
    in.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
    in.directivity.dipoleWeight = r.rangef(0, 1);
    in.directivity.dipolePower = r.rangef(0, 4);
    in.occlusionType = (IPLOcclusionType)r.pick(2);
    in.occlusionRadius = r.rangef(0.01f, 2.0f);
    in.numOcclusionSamples = 1 + r.pick(ss.maxNumOcclusionSamples);
    in.numTransmissionRays = 1 + r.pick(8);
    in.bakedDataIdentifier = id;
    in.pathingProbes = batch;
    in.visRadius = r.rangef(0.01f, 2.0f);
    in.visThreshold = r.rangef(0, 1);
    in.visRange = r.rangef(0.1f, 50.0f);
    in.pathingOrder = ss.maxOrder;
    in.enableValidation = (r.u8() & 1) ? IPL_TRUE : IPL_FALSE;
    in.findAlternatePaths = (r.u8() & 1) ? IPL_TRUE : IPL_FALSE;
    iplSourceSetInputs(src, flags, &in);
    iplSimulatorSetSharedInputs(sim, flags, &shared);
    iplSimulatorRunDirect(sim);
    iplSimulatorRunPathing(sim);
    {
        IPLSimulationOutputs out{};
        iplSourceGetOutputs(src, flags, &out);
        IPLAudioSettings audio{ss.samplingRate, ss.frameSize};
        IPLPathEffectSettings ps{};
        ps.maxOrder = ss.maxOrder;
        IPLPathEffect peff = nullptr;
        int ch = (ss.maxOrder + 1) * (ss.maxOrder + 1);
        if (iplPathEffectCreate(ctx, &audio, &ps, &peff) == IPL_STATUS_SUCCESS) {
            IPLAudioBuffer mono{}, amb{};
            iplAudioBufferAllocate(ctx, 1, ss.frameSize, &mono);
            iplAudioBufferAllocate(ctx, ch, ss.frameSize, &amb);
            fill_audio(r, mono);
            out.pathing.order = ss.maxOrder;
            iplPathEffectApply(peff, &out.pathing, &mono, &amb);
            iplAudioBufferFree(ctx, &mono);
            iplAudioBufferFree(ctx, &amb);
            iplPathEffectRelease(&peff);
        }
    }
done:
    if (src) { iplSourceRemove(src, sim); iplSimulatorCommit(sim); iplSourceRelease(&src); }
    if (sim) { if (batch) iplSimulatorRemoveProbeBatch(sim, batch); iplSimulatorRelease(&sim); }
    if (batch) iplProbeBatchRelease(&batch);
    if (mesh) iplStaticMeshRelease(&mesh);
    if (scene) iplSceneRelease(&scene);
    iplContextRelease(&ctx);
    return 0;
}
