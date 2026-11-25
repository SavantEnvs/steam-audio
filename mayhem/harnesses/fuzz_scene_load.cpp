// Scene deserializer: iplSceneLoad (IPL_SCENETYPE_DEFAULT) on a serialized object made of the input
// bytes (FlatBuffers Scene root: static meshes, vertices, triangles, materials), then commit (BVH
// build) and re-save.
#include "fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // phonon.h, IPLSerializedObjectSettings: a NULL/empty buffer makes an empty object for writing,
    // not one to deserialize from. A serialized object is a FlatBuffers buffer (SerializedObject wraps a
    // FlatBufferBuilder), and every such buffer starts with its 4-byte root offset, so anything shorter
    // cannot be the output of an ipl*Save call. (Loaders do not verify the buffer: a short one is read
    // past its end in flatbuffers::GetRoot; any longer buffer with bad offsets still reaches that bug.)
    if (size < sizeof(uint32_t)) return -1;
    IPLContext ctx = fuzz_context();
    if (!ctx) return 0;
    std::vector<IPLbyte> buf(data, data + size);
    IPLSerializedObjectSettings ss{buf.data(), buf.size()};
    IPLSerializedObject in = nullptr;
    if (iplSerializedObjectCreate(ctx, &ss, &in) == IPL_STATUS_SUCCESS) {
        IPLSceneSettings sc{};
        sc.type = IPL_SCENETYPE_DEFAULT;
        IPLScene scene = nullptr;
        if (iplSceneLoad(ctx, &sc, in, nullptr, nullptr, &scene) == IPL_STATUS_SUCCESS) {
            iplSceneCommit(scene);
            IPLSerializedObjectSettings os{};
            IPLSerializedObject out = nullptr;
            if (iplSerializedObjectCreate(ctx, &os, &out) == IPL_STATUS_SUCCESS) {
                iplSceneSave(scene, out);
                iplSerializedObjectRelease(&out);
            }
            iplSceneRelease(&scene);
        }
        iplSerializedObjectRelease(&in);
    }
    iplContextRelease(&ctx);
    return 0;
}
