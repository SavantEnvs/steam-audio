// Seed generator (not part of the build): writes the serialized-object seeds for the loader targets
// with the public API. For each of a few room variants (box sizes, 1-3 meshes, 1-3 materials) it saves
// the scene as a Scene, its first mesh as a StaticMesh, and a probe batch (1-4 probes) with baked
// parametric reverb. Usage: gen_seeds <outdir>; outputs scene_<i>.bin, static_mesh_<i>.bin,
// probe_batch_<i>.bin. Several valid seeds per loader matter: almost every mutation of a valid
// buffer crashes the unverified FlatBuffers loaders, so libFuzzer needs a few clean inputs to start.
#include <phonon.h>
#include <stdio.h>
#include <string>
#include <vector>

static const IPLint32 kTris[12][3] = {
    {0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6}, {0, 4, 5}, {0, 5, 1},
    {3, 2, 6}, {3, 6, 7}, {0, 3, 7}, {0, 7, 4}, {1, 5, 6}, {1, 6, 2}};

static const IPLMaterial kMat[3] = {{{0.10f, 0.20f, 0.30f}, 0.05f, {0.100f, 0.050f, 0.030f}},
                                    {{0.02f, 0.03f, 0.05f}, 0.50f, {0.010f, 0.005f, 0.002f}},
                                    {{0.60f, 0.70f, 0.80f}, 0.90f, {0.500f, 0.400f, 0.300f}}};

static void write(IPLSerializedObject o, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(iplSerializedObjectGetData(o), 1, iplSerializedObjectGetSize(o), f);
    fclose(f);
}

struct Variant { float sx, sy, sz; int meshes, materials, probes; };

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : ".";
    const Variant vars[] = {{4, 2, 5, 1, 2, 2}, {1, 1, 1, 1, 1, 1}, {10, 3, 8, 2, 3, 3},
                            {2, 5, 2, 3, 2, 4}, {20, 4, 20, 1, 3, 2}, {3, 3, 6, 2, 1, 1}};
    IPLContextSettings cs{}; cs.version = STEAMAUDIO_VERSION;
    IPLContext ctx; iplContextCreate(&cs, &ctx);
    IPLSerializedObjectSettings os{};
    IPLSerializedObject o;
    int idx = 0;
    for (const Variant& V : vars) {
        IPLSceneSettings ss{}; ss.type = IPL_SCENETYPE_DEFAULT;
        IPLScene scene; iplSceneCreate(ctx, &ss, &scene);
        std::vector<IPLStaticMesh> meshes;
        for (int m = 0; m < V.meshes; ++m) {
            float ox = 3.0f * V.sx * m;  // meshes side by side
            IPLVector3 v[8] = {{ox - V.sx, -V.sy, -V.sz}, {ox + V.sx, -V.sy, -V.sz}, {ox + V.sx, V.sy, -V.sz},
                               {ox - V.sx, V.sy, -V.sz},  {ox - V.sx, -V.sy, V.sz},  {ox + V.sx, -V.sy, V.sz},
                               {ox + V.sx, V.sy, V.sz},   {ox - V.sx, V.sy, V.sz}};
            IPLTriangle t[12]; IPLint32 mi[12];
            for (int i = 0; i < 12; ++i) { for (int k = 0; k < 3; ++k) t[i].indices[k] = kTris[i][k]; mi[i] = (i + m) % V.materials; }
            IPLMaterial mat[3] = {kMat[0], kMat[1], kMat[2]};
            IPLStaticMeshSettings ms{8, 12, V.materials, v, t, mi, mat};
            IPLStaticMesh mesh; iplStaticMeshCreate(scene, &ms, &mesh);
            iplStaticMeshAdd(mesh, scene);
            meshes.push_back(mesh);
        }
        iplSceneCommit(scene);
        std::string n = std::to_string(idx++);
        iplSerializedObjectCreate(ctx, &os, &o); iplSceneSave(scene, o); write(o, out + "/scene_" + n + ".bin"); iplSerializedObjectRelease(&o);
        iplSerializedObjectCreate(ctx, &os, &o); iplStaticMeshSave(meshes[0], o); write(o, out + "/static_mesh_" + n + ".bin"); iplSerializedObjectRelease(&o);

        IPLProbeBatch batch; iplProbeBatchCreate(ctx, &batch);
        for (int p = 0; p < V.probes; ++p)
            iplProbeBatchAddProbe(batch, IPLSphere{{0.5f * V.sx * (p % 2), 0, 0.5f * V.sz * (p / 2) - 0.25f * V.sz}, 2.0f});
        iplProbeBatchCommit(batch);
        IPLReflectionsBakeParams bp{};
        bp.scene = scene; bp.probeBatch = batch; bp.sceneType = IPL_SCENETYPE_DEFAULT;
        bp.identifier.type = IPL_BAKEDDATATYPE_REFLECTIONS; bp.identifier.variation = IPL_BAKEDDATAVARIATION_REVERB;
        bp.bakeFlags = IPL_REFLECTIONSBAKEFLAGS_BAKEPARAMETRIC;
        bp.numRays = 256; bp.numDiffuseSamples = 16; bp.numBounces = 4;
        bp.simulatedDuration = 0.5f; bp.savedDuration = 0.5f; bp.order = 0; bp.numThreads = 1;
        bp.irradianceMinDistance = 1.0f; bp.bakeBatchSize = 1;
        iplReflectionsBakerBake(ctx, &bp, nullptr, nullptr);
        iplSerializedObjectCreate(ctx, &os, &o); iplProbeBatchSave(batch, o); write(o, out + "/probe_batch_" + n + ".bin"); iplSerializedObjectRelease(&o);
        printf("variant %s ok probes=%d\n", n.c_str(), iplProbeBatchGetNumProbes(batch));
        iplProbeBatchRelease(&batch);
        for (auto& m : meshes) iplStaticMeshRelease(&m);
        iplSceneRelease(&scene);
    }
    return 0;
}
