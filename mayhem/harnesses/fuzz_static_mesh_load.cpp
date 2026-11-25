// Static mesh deserializer: iplStaticMeshLoad into a default scene from a serialized object made of
// the input bytes (FlatBuffers StaticMesh root), then add it to the scene, commit and re-save.
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
    IPLSceneSettings sc{};
    sc.type = IPL_SCENETYPE_DEFAULT;
    IPLScene scene = nullptr;
    if (iplSceneCreate(ctx, &sc, &scene) == IPL_STATUS_SUCCESS) {
        std::vector<IPLbyte> buf(data, data + size);
        IPLSerializedObjectSettings ss{buf.data(), buf.size()};
        IPLSerializedObject in = nullptr;
        if (iplSerializedObjectCreate(ctx, &ss, &in) == IPL_STATUS_SUCCESS) {
            IPLStaticMesh mesh = nullptr;
            if (iplStaticMeshLoad(scene, in, nullptr, nullptr, &mesh) == IPL_STATUS_SUCCESS) {
                iplStaticMeshAdd(mesh, scene);
                iplSceneCommit(scene);
                IPLSerializedObjectSettings os{};
                IPLSerializedObject out = nullptr;
                if (iplSerializedObjectCreate(ctx, &os, &out) == IPL_STATUS_SUCCESS) {
                    iplStaticMeshSave(mesh, out);
                    iplSerializedObjectRelease(&out);
                }
                iplStaticMeshRemove(mesh, scene);
                iplStaticMeshRelease(&mesh);
            }
            iplSerializedObjectRelease(&in);
        }
        iplSceneRelease(&scene);
    }
    iplContextRelease(&ctx);
    return 0;
}
