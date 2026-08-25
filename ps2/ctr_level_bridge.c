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
_Static_assert(sizeof(struct CTRPS2QuadBlockIndexPrefix) == 0x12, "QuadBlock index prefix must remain 0x12 bytes");
_Static_assert(offsetof(struct CTRPS2QuadBlockIndexPrefix, index) == 0x00, "QuadBlock.index bridge offset changed");

int CTRPS2_LevelBridgePackHighLodFaceV3_16(
    qword_t *dst,
    u32 dst_qwords,
    const struct CTRPS2QuadBlockIndexPrefix *block,
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

static const struct CTRPS2QuadBlockIndexPrefix s_fixtureQuadBlock = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8},
};

int CTRPS2_LevelBridgeBenchRun(void)
{
    qword_t packed[CTRPS2_LEVEL_FACE_POSITION_QWORDS] __attribute__((aligned(64)));
    u32 face;

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
