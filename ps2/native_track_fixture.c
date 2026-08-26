#include "native_track_fixture.h"
#include "p2trk.h"

#include <stddef.h>

#define CTRPS2_NATIVE_FIXTURE_CLUSTER_COUNT 4u
#define CTRPS2_NATIVE_FIXTURE_VERTEX_COUNT  4u

/*
 * Four vertices are 24 source bytes for V3-16. Keep each cluster position
 * stream padded to two qwords so every REF begins on a 16-byte boundary.
 */
struct CTRPS2NativeFixturePositions
{
    s16 data[16];
} __attribute__((aligned(16)));

struct CTRPS2NativeFixtureColors
{
    u8 data[CTRPS2_NATIVE_FIXTURE_VERTEX_COUNT * 4u];
} __attribute__((aligned(16)));

struct CTRPS2NativeFixtureUVs
{
    u16 data[CTRPS2_NATIVE_FIXTURE_VERTEX_COUNT * 4u];
} __attribute__((aligned(16)));

struct CTRPS2NativeTrackFixtureBlob
{
    struct CTRPS2P2TrkHeader header;
    struct CTRPS2P2TrkMaterial material;
    struct CTRPS2P2TrkCluster clusters[CTRPS2_NATIVE_FIXTURE_CLUSTER_COUNT];
    struct CTRPS2NativeFixturePositions positions[CTRPS2_NATIVE_FIXTURE_CLUSTER_COUNT];
    struct CTRPS2NativeFixtureColors colors[CTRPS2_NATIVE_FIXTURE_CLUSTER_COUNT];
    struct CTRPS2NativeFixtureUVs uvs[CTRPS2_NATIVE_FIXTURE_CLUSTER_COUNT];
} __attribute__((aligned(64)));

#define CTRPS2_NATIVE_POS_QWORDS \
    ((u16)(sizeof(struct CTRPS2NativeFixturePositions) / 16u))
#define CTRPS2_NATIVE_COLOR_QWORDS \
    ((u16)(sizeof(struct CTRPS2NativeFixtureColors) / 16u))
#define CTRPS2_NATIVE_UV_QWORDS \
    ((u16)(sizeof(struct CTRPS2NativeFixtureUVs) / 16u))

#define CTRPS2_NATIVE_POS_OFFSET(i) \
    (offsetof(struct CTRPS2NativeTrackFixtureBlob, positions) + \
     ((u32)(i) * sizeof(struct CTRPS2NativeFixturePositions)))
#define CTRPS2_NATIVE_COLOR_OFFSET(i) \
    (offsetof(struct CTRPS2NativeTrackFixtureBlob, colors) + \
     ((u32)(i) * sizeof(struct CTRPS2NativeFixtureColors)))
#define CTRPS2_NATIVE_UV_OFFSET(i) \
    (offsetof(struct CTRPS2NativeTrackFixtureBlob, uvs) + \
     ((u32)(i) * sizeof(struct CTRPS2NativeFixtureUVs)))

_Static_assert((sizeof(struct CTRPS2NativeFixturePositions) & 15u) == 0,
               "native position stream must be qword padded");
_Static_assert((sizeof(struct CTRPS2NativeFixtureColors) & 15u) == 0,
               "native color stream must be qword padded");
_Static_assert((sizeof(struct CTRPS2NativeFixtureUVs) & 15u) == 0,
               "native UV stream must be qword padded");
_Static_assert((offsetof(struct CTRPS2NativeTrackFixtureBlob, positions) & 15u) == 0,
               "native position stream base must be DMA aligned");
_Static_assert((offsetof(struct CTRPS2NativeTrackFixtureBlob, colors) & 15u) == 0,
               "native color stream base must be DMA aligned");
_Static_assert((offsetof(struct CTRPS2NativeTrackFixtureBlob, uvs) & 15u) == 0,
               "native UV stream base must be DMA aligned");

/*
 * N1c fixture: four independent p2trk clusters rather than one stitched strip.
 *
 * Cluster 0: near, red quadrant.
 * Cluster 1: farther, blue quadrant, emitted after cluster 0 and overlapping it.
 * Cluster 2/3: independent full-texture perspective references.
 *
 * This simultaneously validates:
 *   - N1b GEQUAL reversed depth across cluster boundaries;
 *   - VIF1 BASE/OFFSET TOP/TOPS alternation;
 *   - four MSCAL/XGKICK jobs in one persistent DMA chain;
 *   - one final pass-level GS FINISH.
 */
static const struct CTRPS2NativeTrackFixtureBlob s_nativeTrack
    __attribute__((aligned(64))) = {
    .header = {
        .magic = CTRPS2_P2TRK_MAGIC,
        .version = CTRPS2_P2TRK_VERSION,
        .header_bytes = sizeof(struct CTRPS2P2TrkHeader),
        .total_bytes = sizeof(struct CTRPS2NativeTrackFixtureBlob),
        .material_count = 1,
        .cluster_count = CTRPS2_NATIVE_FIXTURE_CLUSTER_COUNT,
        .materials_offset = offsetof(struct CTRPS2NativeTrackFixtureBlob, material),
        .clusters_offset = offsetof(struct CTRPS2NativeTrackFixtureBlob, clusters),
        .reserved = {0, 0},
    },
    .material = {
        .texture_id = 0,
        .pass = CTRPS2_P2TRK_PASS_OPAQUE,
        .flags = CTRPS2_P2TRK_MATERIAL_TEXTURED,
        .state_key = 0,
        .reserved = {0, 0},
    },
    .clusters = {
        {
            .material_index = 0,
            .vertex_count = CTRPS2_NATIVE_FIXTURE_VERTEX_COUNT,
            .gs_primitive = 0x04,
            .flags = 0,
            .reserved0 = 0,
            .positions_offset = CTRPS2_NATIVE_POS_OFFSET(0),
            .positions_qwords = CTRPS2_NATIVE_POS_QWORDS,
            .reserved1 = 0,
            .colors_offset = CTRPS2_NATIVE_COLOR_OFFSET(0),
            .colors_qwords = CTRPS2_NATIVE_COLOR_QWORDS,
            .reserved2 = 0,
            .uvs_offset = CTRPS2_NATIVE_UV_OFFSET(0),
            .uvs_qwords = CTRPS2_NATIVE_UV_QWORDS,
            .reserved3 = 0,
            .bounds_min = {-6, -4, 10},
            .bounds_max = {0, 0, 12},
            .reserved4 = 0,
        },
        {
            .material_index = 0,
            .vertex_count = CTRPS2_NATIVE_FIXTURE_VERTEX_COUNT,
            .gs_primitive = 0x04,
            .flags = 0,
            .reserved0 = 0,
            .positions_offset = CTRPS2_NATIVE_POS_OFFSET(1),
            .positions_qwords = CTRPS2_NATIVE_POS_QWORDS,
            .reserved1 = 0,
            .colors_offset = CTRPS2_NATIVE_COLOR_OFFSET(1),
            .colors_qwords = CTRPS2_NATIVE_COLOR_QWORDS,
            .reserved2 = 0,
            .uvs_offset = CTRPS2_NATIVE_UV_OFFSET(1),
            .uvs_qwords = CTRPS2_NATIVE_UV_QWORDS,
            .reserved3 = 0,
            .bounds_min = {-11, -7, 18},
            .bounds_max = {0, 0, 20},
            .reserved4 = 0,
        },
        {
            .material_index = 0,
            .vertex_count = CTRPS2_NATIVE_FIXTURE_VERTEX_COUNT,
            .gs_primitive = 0x04,
            .flags = 0,
            .reserved0 = 0,
            .positions_offset = CTRPS2_NATIVE_POS_OFFSET(2),
            .positions_qwords = CTRPS2_NATIVE_POS_QWORDS,
            .reserved1 = 0,
            .colors_offset = CTRPS2_NATIVE_COLOR_OFFSET(2),
            .colors_qwords = CTRPS2_NATIVE_COLOR_QWORDS,
            .reserved2 = 0,
            .uvs_offset = CTRPS2_NATIVE_UV_OFFSET(2),
            .uvs_qwords = CTRPS2_NATIVE_UV_QWORDS,
            .reserved3 = 0,
            .bounds_min = {-6, 0, 12},
            .bounds_max = {0, 4, 17},
            .reserved4 = 0,
        },
        {
            .material_index = 0,
            .vertex_count = CTRPS2_NATIVE_FIXTURE_VERTEX_COUNT,
            .gs_primitive = 0x04,
            .flags = 0,
            .reserved0 = 0,
            .positions_offset = CTRPS2_NATIVE_POS_OFFSET(3),
            .positions_qwords = CTRPS2_NATIVE_POS_QWORDS,
            .reserved1 = 0,
            .colors_offset = CTRPS2_NATIVE_COLOR_OFFSET(3),
            .colors_qwords = CTRPS2_NATIVE_COLOR_QWORDS,
            .reserved2 = 0,
            .uvs_offset = CTRPS2_NATIVE_UV_OFFSET(3),
            .uvs_qwords = CTRPS2_NATIVE_UV_QWORDS,
            .reserved3 = 0,
            .bounds_min = {0, 0, 12},
            .bounds_max = {6, 4, 18},
            .reserved4 = 0,
        },
    },
    .positions = {
        { .data = {
            -6, -4, 10,   0, -4, 10,  -6,  0, 12,   0,  0, 12,
             0,  0,  0,   0,
        } },
        { .data = {
            -11, -7, 18,   0, -7, 18,  -10,  0, 20,   0,  0, 20,
               0,  0,  0,   0,
        } },
        { .data = {
            -6,  0, 12,   0,  0, 12,  -5,  4, 17,   0,  4, 17,
             0,  0,  0,   0,
        } },
        { .data = {
             0,  0, 12,   6,  0, 13,   0,  4, 17,   5,  4, 18,
             0,  0,  0,   0,
        } },
    },
    .colors = {
        { .data = {
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
        } },
        { .data = {
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
        } },
        { .data = {
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
        } },
        { .data = {
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
        } },
    },
    .uvs = {
        { .data = {
              8,   8,0,0,  504,   8,0,0,
              8, 504,0,0,  504, 504,0,0,
        } },
        { .data = {
              8, 520,0,0,  504, 520,0,0,
              8,1016,0,0,  504,1016,0,0,
        } },
        { .data = {
              8,   8,0,0, 1016,   8,0,0,
              8,1016,0,0, 1016,1016,0,0,
        } },
        { .data = {
              8,   8,0,0, 1016,   8,0,0,
              8,1016,0,0, 1016,1016,0,0,
        } },
    },
};

const void *CTRPS2_NativeTrackFixtureData(void)
{
    return &s_nativeTrack;
}

u32 CTRPS2_NativeTrackFixtureBytes(void)
{
    return sizeof(s_nativeTrack);
}
