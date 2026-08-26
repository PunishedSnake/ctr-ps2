#include "native_track_renderer.h"

#include "native_geometry.h"
#include "p2trk.h"

#include <string.h>

static struct CTRPS2P2TrkView s_track;
static int s_initialized;
static int s_inflight;

/*
 * N1b/N1c prototype camera/projection contract.
 * Column-major matrix consumed directly by the VU1 microprogram:
 *   clip.x =  1.15 * x
 *   clip.y = -1.45 * y
 *   clip.z =  1
 *   clip.w =  z
 *
 * After perspective division depth is 1/z. The geometry header maps that into
 * a positive 16-bit reversed depth range so GS GEQUAL means "nearer wins".
 * This remains a fixed fixture camera, not the final CTR camera transform.
 */
static const float s_nativeObjectToScreen[16] __attribute__((aligned(64))) = {
    1.15f,  0.00f, 0.0f, 0.0f,
    0.00f, -1.45f, 0.0f, 0.0f,
    0.00f,  0.00f, 0.0f, 1.0f,
    0.00f,  0.00f, 1.0f, 0.0f,
};

static int CTRPS2_NativeTrackRendererBuildBatch(
    struct CTRPS2NativeGeometryBatch *batch,
    u32 cluster_index)
{
    struct CTRPS2P2TrkClusterView view;

    if (!CTRPS2_P2TrkGetCluster(&view, &s_track, cluster_index))
        return 0;

    if (view.material->pass != CTRPS2_P2TRK_PASS_OPAQUE)
        return 0;
    if ((view.material->flags & CTRPS2_P2TRK_MATERIAL_TEXTURED) == 0)
        return 0;
    if (view.material->texture_id != 0)
        return 0;

    memset(batch, 0, sizeof(*batch));
    batch->positions_v3_16 = view.positions_v3_16;
    batch->positions_qwords = view.cluster->positions_qwords;
    batch->colors_rgba8 = view.colors_rgba8;
    batch->colors_qwords = view.cluster->colors_qwords;
    batch->uvs_v4_16 = view.uvs_v4_16;
    batch->uvs_qwords = view.cluster->uvs_qwords;
    batch->vertex_count = view.cluster->vertex_count;
    batch->gs_primitive = view.cluster->gs_primitive;
    batch->textured = 1;
    return 1;
}

int CTRPS2_NativeTrackRendererInit(const void *track_data, u32 track_bytes)
{
    u32 cluster_index;

    if (!CTRPS2_P2TrkOpen(&s_track, track_data, track_bytes))
        return 0;
    if (s_track.header->cluster_count == 0 || s_track.header->material_count == 0)
        return 0;

    if (!CTRPS2_NativeGeometryInit())
        return 0;
    if (!CTRPS2_NativeGeometrySetObjectToScreen(s_nativeObjectToScreen))
        return 0;

    /*
     * N1c builds the complete immutable opaque command chain once. Subsequent
     * frame submits do not allocate packet2 objects, gather vertices or rebuild
     * per-cluster VIF commands.
     */
    if (!CTRPS2_NativeGeometryPassBegin(s_track.header->cluster_count))
        return 0;

    for (cluster_index = 0;
         cluster_index < s_track.header->cluster_count;
         ++cluster_index)
    {
        struct CTRPS2NativeGeometryBatch batch;

        if (!CTRPS2_NativeTrackRendererBuildBatch(&batch, cluster_index))
            return 0;
        if (!CTRPS2_NativeGeometryPassAppend(&batch))
            return 0;
    }

    if (!CTRPS2_NativeGeometryPassEnd())
        return 0;

    s_initialized = 1;
    s_inflight = 0;
    return 1;
}

int CTRPS2_NativeTrackRendererSubmit(void)
{
    if (!s_initialized || s_inflight)
        return 0;

    if (!CTRPS2_NativeGeometryPassSubmit())
        return 0;

    s_inflight = 1;
    return 1;
}

void CTRPS2_NativeTrackRendererWait(void)
{
    if (!s_inflight)
        return;

    CTRPS2_NativeGeometryPassWait();
    s_inflight = 0;
}

int CTRPS2_NativeTrackRendererDraw(void)
{
    if (!CTRPS2_NativeTrackRendererSubmit())
        return 0;

    /*
     * The static proof has no useful EE work after submission yet. The split API
     * deliberately keeps the wait movable for the real frame scheduler.
     */
    CTRPS2_NativeTrackRendererWait();
    return 1;
}
