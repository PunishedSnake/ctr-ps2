#include "ctr_level_bridge.h"
#include "geometry_bench.h"

#include <gs_gp.h>
#include <stddef.h>
#include <string.h>

/*
 * Current ctr-native 1P high-LOD grid topology. Each entry names the four
 * QuadBlock index[] slots consumed for one of the four 2x2 sub-faces.
 */
static const u8 s_highLodFaceIndices[CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT][CTRPS2_LEVEL_FACE_VERTEX_COUNT] = {
    {0, 4, 5, 6},
    {4, 1, 6, 7},
    {5, 6, 2, 8},
    {6, 7, 8, 3},
};

_Static_assert(sizeof(struct CTRPS2LevelVertexView) == 0x10, "LevVertex bridge view must remain 0x10 bytes");
_Static_assert(offsetof(struct CTRPS2LevelVertexView, pos) == 0x00, "LevVertex.pos bridge offset changed");
_Static_assert(sizeof(struct CTRPS2QuadBlockRenderPrefix) == 0x2c, "QuadBlock render prefix must remain 0x2c bytes");
_Static_assert(offsetof(struct CTRPS2QuadBlockRenderPrefix, index) == 0x00, "QuadBlock.index bridge offset changed");
_Static_assert(offsetof(struct CTRPS2QuadBlockRenderPrefix, quad_flags) == 0x12, "QuadBlock.flags bridge offset changed");
_Static_assert(offsetof(struct CTRPS2QuadBlockRenderPrefix, draw_order_low) == 0x14, "QuadBlock.draw_order_low bridge offset changed");
_Static_assert(offsetof(struct CTRPS2QuadBlockRenderPrefix, draw_order_high) == 0x18, "QuadBlock.draw_order_high bridge offset changed");
_Static_assert(offsetof(struct CTRPS2QuadBlockRenderPrefix, texture_mid_ref) == 0x1c, "QuadBlock.texture_mid bridge offset changed");

int CTRPS2_LevelBridgePackHighLodFaceV3_16(
    qword_t *dst,
    u32 dst_qwords,
    const struct CTRPS2QuadBlockRenderPrefix *block,
    const struct CTRPS2LevelVertexView *vertices,
    u32 vertex_count,
    u32 face_index)
{
    s16 *out;
    u32 i;

    if (dst == NULL || block == NULL || vertices == NULL)
        return 0;
    if (face_index >= CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT)
        return 0;
    if (dst_qwords < CTRPS2_LEVEL_FACE_POSITION_QWORDS)
        return 0;

    memset(dst, 0, CTRPS2_LEVEL_FACE_POSITION_QWORDS * sizeof(qword_t));
    out = (s16 *)dst;

    for (i = 0; i < CTRPS2_LEVEL_FACE_VERTEX_COUNT; ++i)
    {
        const u32 quad_index = s_highLodFaceIndices[face_index][i];
        const u32 vertex_index = block->index[quad_index];
        const struct CTRPS2LevelVertexView *src;

        if (vertex_index >= vertex_count)
            return 0;

        src = &vertices[vertex_index];
        out[i * 3 + 0] = src->pos[0];
        out[i * 3 + 1] = src->pos[1];
        out[i * 3 + 2] = src->pos[2];
    }

    return CTRPS2_LEVEL_FACE_VERTEX_COUNT;
}

int CTRPS2_LevelBridgeGetHighLodFaceMeta(
    struct CTRPS2LevelHighLodFaceMeta *out,
    const struct CTRPS2QuadBlockRenderPrefix *block,
    u32 face_index)
{
    u32 face_field;
    u32 face_shift;

    if (out == NULL || block == NULL)
        return 0;
    if (face_index >= CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT)
        return 0;

    /*
     * POTWIERDZONE/current source for the ordinary 3x3 high-LOD grid path:
     * - four packed five-bit face fields begin at bit 8;
     * - low three bits select the UV rotation and the next two bits face mode;
     * - bit 31 is the QuadBlock-wide double-sided flag;
     * - SetGridFaceSlot(face) stores face*4, therefore the normal high-LOD
     *   ordering lookup reads byte 0x18+face, i.e. draw_order_high[face].
     *
     * Dynamic subdivision has a broader retail contract: custom slot words can
     * make the ordering lookup read bytes from the original PSX texture-pointer
     * words. Do not reuse this helper for those paths. Their resolved bias must
     * be captured before pointer representation is changed.
     */
    face_shift = 8u + face_index * 5u;
    face_field = (block->draw_order_low >> face_shift) & 0x1fu;

    memset(out, 0, sizeof(*out));
    out->texture_ref = block->texture_mid_ref[face_index];
    out->order_bias = (s8)((block->draw_order_high >> (face_index * 8u)) & 0xffu);
    out->face_field = (u8)face_field;
    out->uv_rotation = (u8)(face_field & 0x7u);
    out->face_mode = (u8)((face_field >> 3) & 0x3u);
    out->double_sided = (u8)((block->draw_order_low >> 31) & 0x1u);
    return 1;
}

int CTRPS2_LevelBridgeResolveRawRetailOrderBias(
    s8 *out_bias,
    const struct CTRPS2QuadBlockRenderPrefix *raw_block,
    u32 slot_word)
{
    u32 byte_offset;
    const u8 *raw_bytes;

    if (out_bias == NULL || raw_block == NULL)
        return 0;

    /* Retail computes 0x18 + (slotWord >> 2) before a signed byte load. */
    byte_offset = 0x18u + (slot_word >> 2);
    if (byte_offset >= sizeof(*raw_block))
        return 0;

    raw_bytes = (const u8 *)raw_block;
    *out_bias = (s8)raw_bytes[byte_offset];
    return 1;
}

/*
 * Source-layout fixture for the bridge itself. The important property is not
 * the coordinates but the memory contract: nine 0x10-byte LevVertex records
 * addressed through QuadBlock.index[9], exactly like current ctr-native level
 * geometry. Retail track data is intentionally not copied into the repository.
 */
static const struct CTRPS2LevelVertexView s_fixtureVertices[CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT] = {
    {{-8, -8, 0}, 0, {0x40, 0x40, 0x40, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{ 8, -8, 0}, 0, {0x70, 0x40, 0x40, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{-8,  8, 0}, 0, {0x40, 0x70, 0x40, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{ 8,  8, 0}, 0, {0x40, 0x40, 0x70, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{ 0, -8, 0}, 0, {0x60, 0x40, 0x40, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{-8,  0, 0}, 0, {0x40, 0x60, 0x40, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{ 0,  0, 0}, 0, {0x70, 0x70, 0x70, 0x00}, {0x30, 0x30, 0x30, 0x00}},
    {{ 8,  0, 0}, 0, {0x60, 0x60, 0x40, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{ 0,  8, 0}, 0, {0x40, 0x60, 0x60, 0x00}, {0x20, 0x20, 0x20, 0x00}},
};

/* Face fields are 5, 10, 15, 20. draw_order_high bytes are -4, 2, 7, -1. */
static const struct CTRPS2QuadBlockRenderPrefix s_fixtureQuadBlock = {
    .index = {0, 1, 2, 3, 4, 5, 6, 7, 8},
    .quad_flags = 0,
    .draw_order_low = 0x80000000u | (5u << 8) | (10u << 13) | (15u << 18) | (20u << 23),
    .draw_order_high = 0xff0702fcu,
    .texture_mid_ref = {0x44332211u, 0x88776655u, 0xccbbaa99u, 0x00ffe eddu},
};

static int CTRPS2_LevelBridgeValidateFixtureMeta(void)
{
    static const u8 expected_face_field[4] = {5, 10, 15, 20};
    static const s8 expected_order_bias[4] = {-4, 2, 7, -1};
    static const u32 raw_slot_words[6] = {0x0, 0xc, 0x18, 0x24, 0x30, 0x3c};
    static const s8 raw_slot_bias[6] = {-4, -1, 0x33, 0x66, (s8)0x99, (s8)0xcc};
    u32 face;
    u32 slot;

    for (face = 0; face < CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT; ++face)
    {
        struct CTRPS2LevelHighLodFaceMeta meta;

        if (!CTRPS2_LevelBridgeGetHighLodFaceMeta(&meta, &s_fixtureQuadBlock, face))
            return 0;
        if (meta.face_field != expected_face_field[face])
            return 0;
        if (meta.uv_rotation != (expected_face_field[face] & 7u))
            return 0;
        if (meta.face_mode != ((expected_face_field[face] >> 3) & 3u))
            return 0;
        if (meta.order_bias != expected_order_bias[face])
            return 0;
        if (!meta.double_sided)
            return 0;
        if (meta.texture_ref != s_fixtureQuadBlock.texture_mid_ref[face])
            return 0;
    }

    for (slot = 0; slot < 6; ++slot)
    {
        s8 bias;

        if (!CTRPS2_LevelBridgeResolveRawRetailOrderBias(
                &bias,
                &s_fixtureQuadBlock,
                raw_slot_words[slot]))
            return 0;
        if (bias != raw_slot_bias[slot])
            return 0;
    }

    return 1;
}

int CTRPS2_LevelBridgeBenchRun(void)
{
    qword_t packed[CTRPS2_LEVEL_FACE_POSITION_QWORDS] __attribute__((aligned(64)));
    u32 face;

    if (!CTRPS2_LevelBridgeValidateFixtureMeta())
        return 0;

    /*
     * Deliberately conservative M2a baseline: four independent face batches,
     * each followed by the existing FINISH fence. This proves the source layout
     * boundary first. Batching the four faces is the next measured change.
     */
    for (face = 0; face < CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT; ++face)
    {
        int count = CTRPS2_LevelBridgePackHighLodFaceV3_16(
            packed,
            CTRPS2_LEVEL_FACE_POSITION_QWORDS,
            &s_fixtureQuadBlock,
            s_fixtureVertices,
            CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT,
            face);

        if (count != CTRPS2_LEVEL_FACE_VERTEX_COUNT)
            return 0;

        if (!CTRPS2_GeometryBenchConfigureV3_16(
                packed,
                (u32)count,
                GS_PRIM_TRIANGLE_STRIP))
            return 0;

        CTRPS2_GeometryBenchSubmit();
        CTRPS2_GeometryBenchWait();
    }

    return 1;
}
