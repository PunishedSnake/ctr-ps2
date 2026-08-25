#include "ctr_level_bridge.h"
#include "geometry_bench.h"

#include <gs_gp.h>
#include <stddef.h>
#include <string.h>

static const u8 s_highLodFaceIndices[CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT][CTRPS2_LEVEL_FACE_VERTEX_COUNT] = {
    {0, 4, 5, 6},
    {4, 1, 6, 7},
    {5, 6, 2, 8},
    {6, 7, 8, 3},
};

/* Real-hardware validated M2c/M3a strip topology. */
static const u8 s_highLodStripQuadIndices[CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT] = {
    0, 4, 5, 6,
    6, 4, 4, 1, 6, 7,
    7, 5, 5, 6, 2, 8,
    8, 6, 6, 7, 8, 3,
};

/*
 * Each strip vertex belongs to a source face and one canonical corner inside
 * that face. Connector vertices deliberately use the attribute endpoint of the
 * position they duplicate, so the connector triangles remain degenerate after
 * per-face UV orientation is applied.
 */
static const u8 s_highLodStripFace[CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT] = {
    0, 0, 0, 0,
    0, 1, 1, 1, 1, 1,
    1, 2, 2, 2, 2, 2,
    2, 3, 3, 3, 3, 3,
};

static const u8 s_highLodStripBaseCorner[CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT] = {
    0, 1, 2, 3,
    3, 0, 0, 1, 2, 3,
    3, 0, 0, 1, 2, 3,
    3, 0, 0, 1, 2, 3,
};

/* GS UV is 12.4 fixed. Half-texel centers cover a 64x64 texture cleanly. */
static const u16 s_debugFaceUv[CTRPS2_LEVEL_FACE_VERTEX_COUNT][2] = {
    {   8,    8},
    {1016,    8},
    {   8, 1016},
    {1016, 1016},
};

/*
 * POTWIERDZONE from current overlay-226 source + R226 scratchInitTable.
 *
 * Retail takes each five-bit face field as an index into a 24-word table. The
 * low three bits select one of eight orientation variants while bits 3..4 pick
 * the topology family. For topology family zero (ordinary quad), combining:
 *   - R226.scratchInitTable[0..7],
 *   - sDrawLevelOvr1P4x1FaceSelectors,
 *   - DrawLevelOvr1P_Select4x1ProjectedIndices(), and
 *   - DrawLevelOvr1P_WriteProjectedUv()
 * yields the following mapping from canonical geometry corner to TextureLayout
 * corner. The mapping is identical for all four high-LOD faces.
 *
 * Keeping this resolved 8x4 table removes retail scratch-table interpretation
 * from the PS2 steady-state renderer. Families one/two are triangle modes and
 * are intentionally rejected by the quad-strip bridge until their topology is
 * represented explicitly.
 */
static const u8 s_retailQuadUvCorner[8][CTRPS2_LEVEL_FACE_VERTEX_COUNT] = {
    {0, 1, 2, 3},
    {2, 0, 3, 1},
    {3, 2, 1, 0},
    {1, 3, 0, 2},
    {0, 2, 1, 3},
    {2, 3, 0, 1},
    {3, 1, 2, 0},
    {1, 0, 3, 2},
};

_Static_assert(sizeof(struct CTRPS2LevelVertexView) == 0x10, "LevVertex bridge view must remain 0x10 bytes");
_Static_assert(offsetof(struct CTRPS2LevelVertexView, pos) == 0x00, "LevVertex.pos bridge offset changed");
_Static_assert(sizeof(struct CTRPS2QuadBlockRenderPrefix) == 0x2c, "QuadBlock render prefix must remain 0x2c bytes");
_Static_assert(offsetof(struct CTRPS2QuadBlockRenderPrefix, index) == 0x00, "QuadBlock.index bridge offset changed");
_Static_assert(offsetof(struct CTRPS2QuadBlockRenderPrefix, quad_flags) == 0x12, "QuadBlock.flags bridge offset changed");
_Static_assert(offsetof(struct CTRPS2QuadBlockRenderPrefix, draw_order_low) == 0x14, "QuadBlock.draw_order_low bridge offset changed");
_Static_assert(offsetof(struct CTRPS2QuadBlockRenderPrefix, draw_order_high) == 0x18, "QuadBlock.draw_order_high bridge offset changed");
_Static_assert(offsetof(struct CTRPS2QuadBlockRenderPrefix, texture_mid_ref) == 0x1c, "QuadBlock.texture_mid bridge offset changed");
_Static_assert(sizeof(s_highLodStripQuadIndices) == CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT,
               "QuadBlock strip topology count changed");
_Static_assert(sizeof(s_highLodStripFace) == CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT,
               "QuadBlock strip face count changed");
_Static_assert(sizeof(s_highLodStripBaseCorner) == CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT,
               "QuadBlock strip corner count changed");

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
     * - low three bits select one of eight orientation variants;
     * - bits 3..4 select the topology family (quad / triangle A / triangle B;
     *   family 3 has no entry in retail's 24-word selector table);
     * - bit 31 is the QuadBlock-wide double-sided flag;
     * - normal high-LOD ordering reads draw_order_high[face].
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

int CTRPS2_LevelBridgePackHighLodQuadBlockStrip(
    qword_t *positions_dst,
    u32 position_dst_qwords,
    qword_t *colors_dst,
    u32 color_dst_qwords,
    qword_t *uvs_dst,
    u32 uv_dst_qwords,
    const struct CTRPS2QuadBlockRenderPrefix *block,
    const struct CTRPS2LevelVertexView *vertices,
    u32 vertex_count)
{
    s16 *position_out;
    u8 *color_out;
    u16 *uv_out;
    u8 face_rotation[CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT];
    u32 face;
    u32 i;

    if (positions_dst == NULL || colors_dst == NULL || uvs_dst == NULL ||
        block == NULL || vertices == NULL)
        return 0;
    if (position_dst_qwords < CTRPS2_LEVEL_QUADBLOCK_STRIP_POSITION_QWORDS)
        return 0;
    if (color_dst_qwords < CTRPS2_LEVEL_QUADBLOCK_STRIP_COLOR_QWORDS)
        return 0;
    if (uv_dst_qwords < CTRPS2_LEVEL_QUADBLOCK_STRIP_UV_QWORDS)
        return 0;

    /*
     * This exact strip has four vertices per face. Retail selector families 1/2
     * intentionally repeat one vertex and represent triangular topology, so do
     * not silently render those as quads. They get a separate native command.
     */
    for (face = 0; face < CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT; ++face)
    {
        struct CTRPS2LevelHighLodFaceMeta meta;

        if (!CTRPS2_LevelBridgeGetHighLodFaceMeta(&meta, block, face))
            return 0;
        if (meta.face_mode != 0)
            return 0;
        face_rotation[face] = meta.uv_rotation;
    }

    memset(positions_dst, 0, CTRPS2_LEVEL_QUADBLOCK_STRIP_POSITION_QWORDS * sizeof(qword_t));
    memset(colors_dst, 0, CTRPS2_LEVEL_QUADBLOCK_STRIP_COLOR_QWORDS * sizeof(qword_t));
    memset(uvs_dst, 0, CTRPS2_LEVEL_QUADBLOCK_STRIP_UV_QWORDS * sizeof(qword_t));
    position_out = (s16 *)positions_dst;
    color_out = (u8 *)colors_dst;
    uv_out = (u16 *)uvs_dst;

    for (i = 0; i < CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT; ++i)
    {
        const u32 quad_index = s_highLodStripQuadIndices[i];
        const u32 vertex_index = block->index[quad_index];
        const u32 strip_face = s_highLodStripFace[i];
        const u32 base_corner = s_highLodStripBaseCorner[i];
        const u32 uv_corner = s_retailQuadUvCorner[face_rotation[strip_face]][base_corner];
        const struct CTRPS2LevelVertexView *src;

        if (vertex_index >= vertex_count)
            return 0;

        src = &vertices[vertex_index];
        position_out[i * 3 + 0] = src->pos[0];
        position_out[i * 3 + 1] = src->pos[1];
        position_out[i * 3 + 2] = src->pos[2];

        color_out[i * 4 + 0] = src->color_hi[0];
        color_out[i * 4 + 1] = src->color_hi[1];
        color_out[i * 4 + 2] = src->color_hi[2];
        color_out[i * 4 + 3] = 0x80;

        uv_out[i * 4 + 0] = s_debugFaceUv[uv_corner][0];
        uv_out[i * 4 + 1] = s_debugFaceUv[uv_corner][1];
        uv_out[i * 4 + 2] = 0;
        uv_out[i * 4 + 3] = 0;
    }

    return CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT;
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

    byte_offset = 0x18u + (slot_word >> 2);
    if (byte_offset >= sizeof(*raw_block))
        return 0;

    raw_bytes = (const u8 *)raw_block;
    *out_bias = (s8)raw_bytes[byte_offset];
    return 1;
}

/*
 * Metadata-only fixture keeps the earlier mixed face modes so the 5-bit field
 * decoder and raw retail ordering side-channel remain covered independently of
 * the M3b quad-only visual test.
 */
static const struct CTRPS2QuadBlockRenderPrefix s_metaFixtureQuadBlock = {
    .index = {0, 1, 2, 3, 4, 5, 6, 7, 8},
    .quad_flags = 0,
    .draw_order_low = 0x80000000u | (5u << 8) | (10u << 13) | (15u << 18) | (20u << 23),
    .draw_order_high = 0xff0702fcu,
    .texture_mid_ref = {0x44332211u, 0x88776655u, 0xccbbaa99u, 0x00ffeeddU},
};

/* M3b block A shows orientations 0..3, block B shows 4..7. */
static const struct CTRPS2QuadBlockRenderPrefix s_rotationFixtureA = {
    .index = {0, 1, 2, 3, 4, 5, 6, 7, 8},
    .quad_flags = 0,
    .draw_order_low = (0u << 8) | (1u << 13) | (2u << 18) | (3u << 23),
    .draw_order_high = 0,
    .texture_mid_ref = {1, 1, 1, 1},
};

static const struct CTRPS2QuadBlockRenderPrefix s_rotationFixtureB = {
    .index = {0, 1, 2, 3, 4, 5, 6, 7, 8},
    .quad_flags = 0,
    .draw_order_low = (4u << 8) | (5u << 13) | (6u << 18) | (7u << 23),
    .draw_order_high = 0,
    .texture_mid_ref = {1, 1, 1, 1},
};

/* Neutral vertex color makes the diagnostic DECAL texture the visible signal. */
static const struct CTRPS2LevelVertexView s_rotationBaseVertices[CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT] = {
    {{-4, -4, 0}, 0, {0x80, 0x80, 0x80, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{ 4, -4, 0}, 0, {0x80, 0x80, 0x80, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{-4,  4, 0}, 0, {0x80, 0x80, 0x80, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{ 4,  4, 0}, 0, {0x80, 0x80, 0x80, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{ 0, -4, 0}, 0, {0x80, 0x80, 0x80, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{-4,  0, 0}, 0, {0x80, 0x80, 0x80, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{ 0,  0, 0}, 0, {0x80, 0x80, 0x80, 0x00}, {0x30, 0x30, 0x30, 0x00}},
    {{ 4,  0, 0}, 0, {0x80, 0x80, 0x80, 0x00}, {0x20, 0x20, 0x20, 0x00}},
    {{ 0,  4, 0}, 0, {0x80, 0x80, 0x80, 0x00}, {0x20, 0x20, 0x20, 0x00}},
};

static void CTRPS2_LevelBridgeBuildTranslatedFixture(
    struct CTRPS2LevelVertexView *dst,
    s16 x_offset)
{
    u32 i;

    memcpy(dst, s_rotationBaseVertices, sizeof(s_rotationBaseVertices));
    for (i = 0; i < CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT; ++i)
        dst[i].pos[0] = (s16)(dst[i].pos[0] + x_offset);
}

static int CTRPS2_LevelBridgeValidateUvTable(void)
{
    u32 rotation;
    u32 other;

    for (rotation = 0; rotation < 8; ++rotation)
    {
        u32 seen = 0;
        u32 corner;

        for (corner = 0; corner < CTRPS2_LEVEL_FACE_VERTEX_COUNT; ++corner)
        {
            const u32 mapped = s_retailQuadUvCorner[rotation][corner];
            if (mapped >= CTRPS2_LEVEL_FACE_VERTEX_COUNT)
                return 0;
            if (seen & (1u << mapped))
                return 0;
            seen |= 1u << mapped;
        }
        if (seen != 0x0fu)
            return 0;

        for (other = 0; other < rotation; ++other)
        {
            if (memcmp(s_retailQuadUvCorner[rotation],
                       s_retailQuadUvCorner[other],
                       CTRPS2_LEVEL_FACE_VERTEX_COUNT) == 0)
                return 0;
        }
    }

    return 1;
}

static int CTRPS2_LevelBridgeValidateFixtureMeta(void)
{
    static const u8 expected_face_field[4] = {5, 10, 15, 20};
    static const s8 expected_order_bias[4] = {-4, 2, 7, -1};
    static const u32 raw_slot_words[6] = {0x0, 0xc, 0x18, 0x24, 0x30, 0x3c};
    static const s8 raw_slot_bias[6] = {-4, -1, 0x33, 0x66, (s8)0x99, (s8)0xcc};
    u32 face;
    u32 slot;

    if (!CTRPS2_LevelBridgeValidateUvTable())
        return 0;

    for (face = 0; face < CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT; ++face)
    {
        struct CTRPS2LevelHighLodFaceMeta meta;

        if (!CTRPS2_LevelBridgeGetHighLodFaceMeta(&meta, &s_metaFixtureQuadBlock, face))
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
        if (meta.texture_ref != s_metaFixtureQuadBlock.texture_mid_ref[face])
            return 0;
    }

    for (slot = 0; slot < 6; ++slot)
    {
        s8 bias;

        if (!CTRPS2_LevelBridgeResolveRawRetailOrderBias(
                &bias,
                &s_metaFixtureQuadBlock,
                raw_slot_words[slot]))
            return 0;
        if (bias != raw_slot_bias[slot])
            return 0;
    }

    return 1;
}

static int CTRPS2_LevelBridgeDrawFixture(
    const struct CTRPS2QuadBlockRenderPrefix *block,
    const struct CTRPS2LevelVertexView *vertices)
{
    qword_t positions[CTRPS2_LEVEL_QUADBLOCK_STRIP_POSITION_QWORDS]
        __attribute__((aligned(64)));
    qword_t colors[CTRPS2_LEVEL_QUADBLOCK_STRIP_COLOR_QWORDS]
        __attribute__((aligned(64)));
    qword_t uvs[CTRPS2_LEVEL_QUADBLOCK_STRIP_UV_QWORDS]
        __attribute__((aligned(64)));
    int count;

    count = CTRPS2_LevelBridgePackHighLodQuadBlockStrip(
        positions,
        CTRPS2_LEVEL_QUADBLOCK_STRIP_POSITION_QWORDS,
        colors,
        CTRPS2_LEVEL_QUADBLOCK_STRIP_COLOR_QWORDS,
        uvs,
        CTRPS2_LEVEL_QUADBLOCK_STRIP_UV_QWORDS,
        block,
        vertices,
        CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT);
    if (count != CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT)
        return 0;

    if (!CTRPS2_GeometryBenchConfigureV3_16_RGBA8_UV16(
            positions,
            colors,
            uvs,
            (u32)count,
            GS_PRIM_TRIANGLE_STRIP))
        return 0;

    CTRPS2_GeometryBenchSubmit();
    CTRPS2_GeometryBenchWait();
    return 1;
}

int CTRPS2_LevelBridgeBenchRun(void)
{
    struct CTRPS2LevelVertexView vertices_a[CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT];
    struct CTRPS2LevelVertexView vertices_b[CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT];

    if (!CTRPS2_LevelBridgeValidateFixtureMeta())
        return 0;

    /*
     * M3b visual oracle: two half-size blocks cover all eight retail orientation
     * variants simultaneously. Left block = 0..3, right block = 4..7. The same
     * asymmetric resident texture makes rotations/mirrors immediately visible.
     */
    CTRPS2_LevelBridgeBuildTranslatedFixture(vertices_a, -5);
    CTRPS2_LevelBridgeBuildTranslatedFixture(vertices_b, 5);

    if (!CTRPS2_LevelBridgeDrawFixture(&s_rotationFixtureA, vertices_a))
        return 0;
    if (!CTRPS2_LevelBridgeDrawFixture(&s_rotationFixtureB, vertices_b))
        return 0;

    return 1;
}
