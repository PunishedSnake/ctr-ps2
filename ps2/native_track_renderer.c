#include "native_track_renderer.h"

#include "native_geometry.h"
#include "p2trk.h"

#include <string.h>

static struct CTRPS2P2TrkView s_track;
static int s_initialized;

/*
 * First native camera/projection contract.
 * Column-major matrix consumed directly by the VU1 microprogram:
 *   clip.x = 1.15 * x
 *   clip.y = -1.45 * y
 *   clip.z = z - 1
 *   clip.w = z
 *
 * This deliberately proves real perspective division in the native path. It is
 * a fixed prototype camera, not the final CTR camera transform.
 */
static const float s_nativeObjectToScreen[16] __attribute__((aligned(64))) = {
    1.15f,  0.00f, 0.0f, 0.0f,
    0.00f, -1.45f, 0.0f, 0.0f,
    0.00f,  0.00f, 1.0f, 1.0f,
    0.00f,  0.00f,-1.0f, 0.0f,
};

int CTRPS2_NativeTrackRendererInit(const void *track_data, u32 track_bytes)
{
    if (!CTRPS2_P2TrkOpen(&s_track, track_data, track_bytes))
        return 0;
    if (s_track.header->cluster_count == 0 || s_track.header->material_count == 0)
        return 0;

    if (!CTRPS2_NativeGeometryInit())
        return 0;
    if (!CTRPS2_NativeGeometrySetObjectToScreen(s_nativeObjectToScreen))
        return 0;

    s_initialized = 1;
    return 1;
}

int CTRPS2_NativeTrackRendererDraw(void)
{
    u32 cluster_index;

    if (!s_initialized)
        return 0;

    for (cluster_index = 0;
         cluster_index < s_track.header->cluster_count;
         ++cluster_index)
    {
        struct CTRPS2P2TrkClusterView view;
        struct CTRPS2NativeGeometryBatch batch;

        if (!CTRPS2_P2TrkGetCluster(&view, &s_track, cluster_index))
            return 0;

        /*
         * Native milestone N0 supports the first shipping pass only:
         * opaque, textured static geometry using resident texture slot 0.
         * Other passes become explicit renderer paths instead of PS1 packet
         * flags leaking into this command stream.
         */
        if (view.material->pass != CTRPS2_P2TRK_PASS_OPAQUE)
            return 0;
        if ((view.material->flags & CTRPS2_P2TRK_MATERIAL_TEXTURED) == 0)
            return 0;
        if (view.material->texture_id != 0)
            return 0;

        memset(&batch, 0, sizeof(batch));
        batch.positions_v3_16 = view.positions_v3_16;
        batch.positions_qwords = view.cluster->positions_qwords;
        batch.colors_rgba8 = view.colors_rgba8;
        batch.colors_qwords = view.cluster->colors_qwords;
        batch.uvs_v4_16 = view.uvs_v4_16;
        batch.uvs_qwords = view.cluster->uvs_qwords;
        batch.vertex_count = view.cluster->vertex_count;
        batch.gs_primitive = view.cluster->gs_primitive;
        batch.textured = 1;

        if (!CTRPS2_NativeGeometryPrepare(&batch))
            return 0;

        CTRPS2_NativeGeometrySubmit();

        /*
         * CURRENT IMPLEMENTATION correctness baseline. N1 replaces this
         * per-cluster retirement with command arenas and late frame/pass waits.
         */
        CTRPS2_NativeGeometryWait();
    }

    return 1;
}
