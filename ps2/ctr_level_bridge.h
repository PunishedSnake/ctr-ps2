#ifndef CTR_PS2_LEVEL_BRIDGE_H
#define CTR_PS2_LEVEL_BRIDGE_H

#include <tamtypes.h>

#define CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT 9
#define CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT 4
#define CTRPS2_LEVEL_FACE_VERTEX_COUNT 4
#define CTRPS2_LEVEL_FACE_POSITION_QWORDS 2

/*
 * Four independent 4-vertex strips are joined by degenerate connectors. The
 * established 22-vertex sequence is now real-hardware validated by M2c.
 */
#define CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT \
    (CTRPS2_LEVEL_FACE_VERTEX_COUNT + (CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT - 1) * 6)
#define CTRPS2_LEVEL_QUADBLOCK_STRIP_POSITION_QWORDS \
    ((CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT * 3u * sizeof(s16) + 15u) / 16u)
#define CTRPS2_LEVEL_QUADBLOCK_STRIP_COLOR_QWORDS \
    ((CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT * 4u * sizeof(u8) + 15u) / 16u)
#define CTRPS2_LEVEL_QUADBLOCK_STRIP_UV_QWORDS \
    ((CTRPS2_LEVEL_QUADBLOCK_STRIP_VERTEX_COUNT * 4u * sizeof(u16) + 15u) / 16u)

/*
 * PS2-owned read-only views of the current ctr-native level layout. They expose
 * only fields needed by the bridge and are not a second full struct authority.
 */
struct CTRPS2LevelVertexView
{
    s16 pos[3];
    u16 flags;
    u8 color_hi[4];
    u8 color_lo[4];
};

struct CTRPS2QuadBlockRenderPrefix
{
    u16 index[CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT];
    u16 quad_flags;
    u32 draw_order_low;
    u32 draw_order_high;
    u32 texture_mid_ref[CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT];
};

struct CTRPS2LevelHighLodFaceMeta
{
    u32 texture_ref;
    s8 order_bias;
    u8 face_field;
    u8 uv_rotation;
    u8 face_mode;
    u8 double_sided;
    u8 reserved[3];
};

int CTRPS2_LevelBridgePackHighLodFaceV3_16(
    qword_t *dst,
    u32 dst_qwords,
    const struct CTRPS2QuadBlockRenderPrefix *block,
    const struct CTRPS2LevelVertexView *vertices,
    u32 vertex_count,
    u32 face_index);

/*
 * Expand one ordinary high-LOD QuadBlock to the current PS2 consumer streams:
 * signed V3-16 position, RGBA8 color and deterministic V4-16 UV. M3a assigns
 * the same diagnostic 0.5..63.5 texel rectangle to each face. Retail UV
 * rotation is deliberately not applied until its source contract is complete.
 */
int CTRPS2_LevelBridgePackHighLodQuadBlockStrip(
    qword_t *positions_dst,
    u32 position_dst_qwords,
    qword_t *colors_dst,
    u32 color_dst_qwords,
    qword_t *uvs_dst,
    u32 uv_dst_qwords,
    const struct CTRPS2QuadBlockRenderPrefix *block,
    const struct CTRPS2LevelVertexView *vertices,
    u32 vertex_count);

int CTRPS2_LevelBridgeGetHighLodFaceMeta(
    struct CTRPS2LevelHighLodFaceMeta *out,
    const struct CTRPS2QuadBlockRenderPrefix *block,
    u32 face_index);

int CTRPS2_LevelBridgeResolveRawRetailOrderBias(
    s8 *out_bias,
    const struct CTRPS2QuadBlockRenderPrefix *raw_block,
    u32 slot_word);

int CTRPS2_LevelBridgeBenchRun(void);

#endif
