#include "native_track_fixture.h"
#include "p2trk.h"

#include <stddef.h>

#define CTRPS2_NATIVE_FIXTURE_VERTEX_COUNT 22u

struct CTRPS2NativeFixturePositions
{
    s16 data[CTRPS2_NATIVE_FIXTURE_VERTEX_COUNT * 3u];
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
    struct CTRPS2P2TrkCluster cluster;
    struct CTRPS2NativeFixturePositions positions;
    struct CTRPS2NativeFixtureColors colors;
    struct CTRPS2NativeFixtureUVs uvs;
} __attribute__((aligned(64)));

#define CTRPS2_NATIVE_POS_QWORDS \
    ((u16)(sizeof(struct CTRPS2NativeFixturePositions) / 16u))
#define CTRPS2_NATIVE_COLOR_QWORDS \
    ((u16)(sizeof(struct CTRPS2NativeFixtureColors) / 16u))
#define CTRPS2_NATIVE_UV_QWORDS \
    ((u16)(sizeof(struct CTRPS2NativeFixtureUVs) / 16u))

_Static_assert((sizeof(struct CTRPS2NativeFixturePositions) & 15u) == 0,
               "native position stream must be qword padded");
_Static_assert((sizeof(struct CTRPS2NativeFixtureColors) & 15u) == 0,
               "native color stream must be qword padded");
_Static_assert((sizeof(struct CTRPS2NativeFixtureUVs) & 15u) == 0,
               "native UV stream must be qword padded");
_Static_assert((offsetof(struct CTRPS2NativeTrackFixtureBlob, positions) & 15u) == 0,
               "native position stream offset must be DMA aligned");
_Static_assert((offsetof(struct CTRPS2NativeTrackFixtureBlob, colors) & 15u) == 0,
               "native color stream offset must be DMA aligned");
_Static_assert((offsetof(struct CTRPS2NativeTrackFixtureBlob, uvs) & 15u) == 0,
               "native UV stream offset must be DMA aligned");

/*
 * First true PS2-owned static cluster.
 *
 * This is deliberately not built from QuadBlock/LevVertex at runtime. The 22
 * vertices are already arranged as one GS triangle strip with degenerate
 * connectors between four textured faces. Z increases toward the lower row so
 * the native perspective matrix produces visible depth instead of another flat
 * compatibility rectangle.
 */
static const struct CTRPS2NativeTrackFixtureBlob s_nativeTrack
    __attribute__((aligned(64))) = {
    .header = {
        .magic = CTRPS2_P2TRK_MAGIC,
        .version = CTRPS2_P2TRK_VERSION,
        .header_bytes = sizeof(struct CTRPS2P2TrkHeader),
        .total_bytes = sizeof(struct CTRPS2NativeTrackFixtureBlob),
        .material_count = 1,
        .cluster_count = 1,
        .materials_offset = offsetof(struct CTRPS2NativeTrackFixtureBlob, material),
        .clusters_offset = offsetof(struct CTRPS2NativeTrackFixtureBlob, cluster),
        .reserved = {0, 0},
    },
    .material = {
        .texture_id = 0,
        .pass = CTRPS2_P2TRK_PASS_OPAQUE,
        .flags = CTRPS2_P2TRK_MATERIAL_TEXTURED,
        .state_key = 0,
        .reserved = {0, 0},
    },
    .cluster = {
        .material_index = 0,
        .vertex_count = CTRPS2_NATIVE_FIXTURE_VERTEX_COUNT,
        .gs_primitive = 0x04, /* GS_PRIM_TRIANGLE_STRIP */
        .flags = 0,
        .reserved0 = 0,
        .positions_offset = offsetof(struct CTRPS2NativeTrackFixtureBlob, positions),
        .positions_qwords = CTRPS2_NATIVE_POS_QWORDS,
        .reserved1 = 0,
        .colors_offset = offsetof(struct CTRPS2NativeTrackFixtureBlob, colors),
        .colors_qwords = CTRPS2_NATIVE_COLOR_QWORDS,
        .reserved2 = 0,
        .uvs_offset = offsetof(struct CTRPS2NativeTrackFixtureBlob, uvs),
        .uvs_qwords = CTRPS2_NATIVE_UV_QWORDS,
        .reserved3 = 0,
        .bounds_min = {-6, -4, 10},
        .bounds_max = {6, 4, 18},
        .reserved4 = 0,
    },
    .positions = {
        .data = {
            -6, -4, 10,   0, -4, 10,  -6,  0, 12,   0,  0, 12,
             0,  0, 12,   0, -4, 10,   0, -4, 10,   6, -4, 11,   0,  0, 12,   6,  0, 13,
             6,  0, 13,  -6,  0, 12,  -6,  0, 12,   0,  0, 12,  -5,  4, 17,   0,  4, 17,
             0,  4, 17,   0,  0, 12,   0,  0, 12,   6,  0, 13,   0,  4, 17,   5,  4, 18,
        },
    },
    .colors = {
        .data = {
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
            0x80,0x80,0x80,0x80, 0x80,0x80,0x80,0x80,
        },
    },
    .uvs = {
        .data = {
               8,   8,0,0, 1016,   8,0,0,    8,1016,0,0, 1016,1016,0,0,
            1016,1016,0,0,    8,   8,0,0,    8,   8,0,0, 1016,   8,0,0,    8,1016,0,0, 1016,1016,0,0,
            1016,1016,0,0,    8,   8,0,0,    8,   8,0,0, 1016,   8,0,0,    8,1016,0,0, 1016,1016,0,0,
            1016,1016,0,0,    8,   8,0,0,    8,   8,0,0, 1016,   8,0,0,    8,1016,0,0, 1016,1016,0,0,
        },
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
