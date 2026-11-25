// Probe batch deserializer: iplProbeBatchLoad on a serialized object made of the input bytes
// (FlatBuffers ProbeBatch root: probes, probe tree, baked reflection/pathing data), then the
// read-side API on the loaded batch and a re-save.
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
        IPLProbeBatch batch = nullptr;
        if (iplProbeBatchLoad(ctx, in, &batch) == IPL_STATUS_SUCCESS) {
            int n = iplProbeBatchGetNumProbes(batch);
            IPLBakedDataIdentifier ids[3] = {};
            ids[0].type = IPL_BAKEDDATATYPE_REFLECTIONS; ids[0].variation = IPL_BAKEDDATAVARIATION_REVERB;
            ids[1].type = IPL_BAKEDDATATYPE_REFLECTIONS; ids[1].variation = IPL_BAKEDDATAVARIATION_STATICLISTENER;
            ids[2].type = IPL_BAKEDDATATYPE_PATHING; ids[2].variation = IPL_BAKEDDATAVARIATION_DYNAMIC;
            for (auto& id : ids) {
                iplProbeBatchGetDataSize(batch, &id);
                float reverb[IPL_NUM_BANDS];
                if (n > 0 && &id == &ids[0]) iplProbeBatchGetReverb(batch, &id, 0, reverb);
            }
            iplProbeBatchCommit(batch);
            IPLSerializedObjectSettings os{};
            IPLSerializedObject out = nullptr;
            if (iplSerializedObjectCreate(ctx, &os, &out) == IPL_STATUS_SUCCESS) {
                iplProbeBatchSave(batch, out);
                iplSerializedObjectRelease(&out);
            }
            iplProbeBatchRelease(&batch);
        }
        iplSerializedObjectRelease(&in);
    }
    iplContextRelease(&ctx);
    return 0;
}
