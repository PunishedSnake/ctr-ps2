#ifndef CTR_PS2_LEVEL_BRIDGE_H
#define CTR_PS2_LEVEL_BRIDGE_H

#include <tamtypes.h>

#define CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT 9
#define CTRPS2_LEVEL_HIGH_LOD_FACE_COUNT 4
#define CTRPS2_LEVEL_FACE_VERTEX_COUNT 4
#define CTRPS2_LEVEL_FACE_POSITION_QWORDS 2

/*
 * PS2-owned read-only views of the current ctr-native level layout.
 *
 * These are deliberately only the prefixes/fields needed by M2a. They are not
 * a second authoritative definition of the full game structs. Current source:
 *   LevVertex  = SVec3 pos + flags + color_hi[4] + color_lo[4] (0x10 bytes)
 *   QuadBlock  = u16 index[9] at 0x00, flags at 0x12, draw-order words at
 *                0x14/0x18 and four mid-texture references at 0x1c..0x2b.
 *
 * Keeping the bridge narrow lets the standalone PS2 target consume the retail-
 * shaped layout without pulling the PS1/PsyQ compatibility headers into the EE
 * build. Once the full game target is PS2-neutral this adapter can disappear.
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

/*
 * Source semantics for one ordinary high-LOD 3x3-grid face. `texture_ref` is
 * deliberately still a source-layout reference, not a GS texture handle.
 * Texture conversion/residency belongs to the later asset boundary.
 */
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

/*
 * Expand one current CTR high-LOD 3x3-grid face to a VIF-ready V3-16 stream.
 * This runtime gather is an integration baseline, not the final asset format.
 */
int CTRPS2_LevelBridgePackHighLodFaceV3_16(
    qword_t *dst,
    u32 dst_qwords,
    const struct CTRPS2QuadBlockRenderPrefix *block,
    const struct CTRPS2LevelVertexView *vertices,
    u32 vertex_count,
    u32 face_index);

/* Decode the ordinary high-LOD face state before PS1-specific rendering. */
int CTRPS2_LevelBridgeGetHighLodFaceMeta(
    struct CTRPS2LevelHighLodFaceMeta *out,
    const struct CTRPS2QuadBlockRenderPrefix *block,
    u32 face_index);

/*
 * Resolve retail's byte-addressed ordering side-channel from an unmodified raw
 * QuadBlock prefix. This must run before texture pointer words are rebased or
 * replaced by native/GS handles. It is intended for asset conversion, not the
 * steady-state draw loop.
 */
int CTRPS2_LevelBridgeResolveRawRetailOrderBias(
    s8 *out_bias,
    const struct CTRPS2QuadBlockRenderPrefix *raw_block,
    u32 slot_word);

/* Draw a source-layout fixture through the current M1 VIF1/VU1 path. */
int CTRPS2_LevelBridgeBenchRun(void);

#endif
