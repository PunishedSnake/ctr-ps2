#ifndef CTR_PS2_P2TRK_H
#define CTR_PS2_P2TRK_H

#include <tamtypes.h>

#define CTRPS2_P2TRK_MAGIC   0x4b543250u /* 'P2TK' little-endian */
#define CTRPS2_P2TRK_VERSION 1u

#define CTRPS2_P2TRK_STREAM_ALIGNMENT 16u

enum CTRPS2P2TrkPass
{
    CTRPS2_P2TRK_PASS_OPAQUE = 0,
    CTRPS2_P2TRK_PASS_CUTOUT = 1,
    CTRPS2_P2TRK_PASS_TRANSLUCENT = 2,
    CTRPS2_P2TRK_PASS_ADDITIVE = 3,
};

enum CTRPS2P2TrkMaterialFlags
{
    CTRPS2_P2TRK_MATERIAL_TEXTURED = 1u << 0,
    CTRPS2_P2TRK_MATERIAL_DOUBLE_SIDED = 1u << 1,
};

enum CTRPS2P2TrkClusterFlags
{
    /* U/V stream stores only two unsigned 16-bit lanes per vertex. */
    CTRPS2_P2TRK_CLUSTER_UV_V2_16 = 1u << 0,
};

struct CTRPS2P2TrkHeader
{
    u32 magic;
    u16 version;
    u16 header_bytes;
    u32 total_bytes;

    u16 material_count;
    u16 cluster_count;
    u32 materials_offset;
    u32 clusters_offset;

    u32 reserved[2];
};

struct CTRPS2P2TrkMaterial
{
    u16 texture_id;
    u8 pass;
    u8 flags;
    u32 state_key;
    u32 reserved[2];
};

struct CTRPS2P2TrkCluster
{
    u16 material_index;
    u16 vertex_count;
    u8 gs_primitive;
    u8 flags;
    u16 reserved0;

    u32 positions_offset;
    u16 positions_qwords;
    u16 reserved1;

    u32 colors_offset;
    u16 colors_qwords;
    u16 reserved2;

    u32 uvs_offset;
    u16 uvs_qwords;
    u16 reserved3;

    s16 bounds_min[3];
    s16 bounds_max[3];
    u32 reserved4;
};

struct CTRPS2P2TrkView
{
    const u8 *base;
    u32 bytes;
    const struct CTRPS2P2TrkHeader *header;
    const struct CTRPS2P2TrkMaterial *materials;
    const struct CTRPS2P2TrkCluster *clusters;
};

struct CTRPS2P2TrkClusterView
{
    const struct CTRPS2P2TrkCluster *cluster;
    const struct CTRPS2P2TrkMaterial *material;
    const void *positions_v3_16;
    const void *colors_rgba8;
    const void *uvs_v4_16;
};

int CTRPS2_P2TrkOpen(
    struct CTRPS2P2TrkView *out,
    const void *data,
    u32 bytes);

int CTRPS2_P2TrkGetCluster(
    struct CTRPS2P2TrkClusterView *out,
    const struct CTRPS2P2TrkView *track,
    u32 cluster_index);

#endif
