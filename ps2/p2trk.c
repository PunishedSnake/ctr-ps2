#include "p2trk.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(struct CTRPS2P2TrkHeader) == 0x20,
               "p2trk header ABI changed");
_Static_assert(sizeof(struct CTRPS2P2TrkMaterial) == 0x10,
               "p2trk material ABI changed");
_Static_assert(sizeof(struct CTRPS2P2TrkCluster) == 0x30,
               "p2trk cluster ABI changed");

static int CTRPS2_P2TrkRangeIsValid(u32 offset, u32 size, u32 total)
{
    if (offset > total)
        return 0;
    return size <= (total - offset);
}

static int CTRPS2_P2TrkStreamIsValid(
    const struct CTRPS2P2TrkView *track,
    u32 offset,
    u32 qwords,
    u32 required_bytes)
{
    u32 bytes;
    const void *ptr;

    if (required_bytes == 0)
        return qwords == 0;
    if (qwords == 0)
        return 0;
    if ((offset & (CTRPS2_P2TRK_STREAM_ALIGNMENT - 1u)) != 0)
        return 0;
    if (qwords > (0xffffffffu / 16u))
        return 0;

    bytes = qwords * 16u;
    if (bytes < required_bytes)
        return 0;
    if (!CTRPS2_P2TrkRangeIsValid(offset, bytes, track->bytes))
        return 0;

    ptr = track->base + offset;
    return (((uintptr_t)ptr & (CTRPS2_P2TRK_STREAM_ALIGNMENT - 1u)) == 0);
}

static int CTRPS2_P2TrkMaterialIsValid(
    const struct CTRPS2P2TrkMaterial *material)
{
    if (material->pass > CTRPS2_P2TRK_PASS_ADDITIVE)
        return 0;

    if (material->flags & CTRPS2_P2TRK_MATERIAL_TEXTURED)
    {
        if (material->texture_width == 0 || material->texture_height == 0)
            return 0;
    }
    else if (material->texture_width != 0 || material->texture_height != 0)
    {
        return 0;
    }

    return 1;
}

int CTRPS2_P2TrkOpen(
    struct CTRPS2P2TrkView *out,
    const void *data,
    u32 bytes)
{
    const struct CTRPS2P2TrkHeader *header;
    u32 material_bytes;
    u32 cluster_bytes;
    u32 material_index;

    if (out == NULL || data == NULL)
        return 0;
    if (bytes < sizeof(struct CTRPS2P2TrkHeader))
        return 0;
    if (((uintptr_t)data & 3u) != 0)
        return 0;

    header = (const struct CTRPS2P2TrkHeader *)data;
    if (header->magic != CTRPS2_P2TRK_MAGIC)
        return 0;
    if (header->version != CTRPS2_P2TRK_VERSION)
        return 0;
    if (header->header_bytes != sizeof(*header))
        return 0;
    if (header->total_bytes < sizeof(*header) || header->total_bytes > bytes)
        return 0;

    material_bytes = (u32)header->material_count * sizeof(struct CTRPS2P2TrkMaterial);
    cluster_bytes = (u32)header->cluster_count * sizeof(struct CTRPS2P2TrkCluster);

    if (!CTRPS2_P2TrkRangeIsValid(
            header->materials_offset,
            material_bytes,
            header->total_bytes))
        return 0;
    if (!CTRPS2_P2TrkRangeIsValid(
            header->clusters_offset,
            cluster_bytes,
            header->total_bytes))
        return 0;

    memset(out, 0, sizeof(*out));
    out->base = (const u8 *)data;
    out->bytes = header->total_bytes;
    out->header = header;
    out->materials = (const struct CTRPS2P2TrkMaterial *)(out->base + header->materials_offset);
    out->clusters = (const struct CTRPS2P2TrkCluster *)(out->base + header->clusters_offset);

    for (material_index = 0; material_index < header->material_count; ++material_index)
    {
        if (!CTRPS2_P2TrkMaterialIsValid(&out->materials[material_index]))
            return 0;
    }

    return 1;
}

int CTRPS2_P2TrkGetCluster(
    struct CTRPS2P2TrkClusterView *out,
    const struct CTRPS2P2TrkView *track,
    u32 cluster_index)
{
    const struct CTRPS2P2TrkCluster *cluster;
    const struct CTRPS2P2TrkMaterial *material;
    u32 position_bytes;
    u32 color_bytes;
    u32 uv_bytes;
    u32 uv_lanes;

    if (out == NULL || track == NULL || track->header == NULL)
        return 0;
    if (cluster_index >= track->header->cluster_count)
        return 0;

    cluster = &track->clusters[cluster_index];
    if (cluster->vertex_count == 0)
        return 0;
    if (cluster->material_index >= track->header->material_count)
        return 0;
    if ((cluster->flags & ~CTRPS2_P2TRK_CLUSTER_UV_V2_16) != 0)
        return 0;

    material = &track->materials[cluster->material_index];

    position_bytes = (u32)cluster->vertex_count * 3u * sizeof(s16);
    color_bytes = (u32)cluster->vertex_count * 4u * sizeof(u8);

    uv_lanes = (cluster->flags & CTRPS2_P2TRK_CLUSTER_UV_V2_16) ? 2u : 4u;
    uv_bytes = (material->flags & CTRPS2_P2TRK_MATERIAL_TEXTURED)
                   ? ((u32)cluster->vertex_count * uv_lanes * sizeof(u16))
                   : 0u;

    if (!CTRPS2_P2TrkStreamIsValid(
            track,
            cluster->positions_offset,
            cluster->positions_qwords,
            position_bytes))
        return 0;
    if (!CTRPS2_P2TrkStreamIsValid(
            track,
            cluster->colors_offset,
            cluster->colors_qwords,
            color_bytes))
        return 0;
    if (!CTRPS2_P2TrkStreamIsValid(
            track,
            cluster->uvs_offset,
            cluster->uvs_qwords,
            uv_bytes))
        return 0;

    memset(out, 0, sizeof(*out));
    out->cluster = cluster;
    out->material = material;
    out->positions_v3_16 = track->base + cluster->positions_offset;
    out->colors_rgba8 = track->base + cluster->colors_offset;
    out->uvs_16 = uv_bytes ? (track->base + cluster->uvs_offset) : NULL;
    return 1;
}
