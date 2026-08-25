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
 *   QuadBlock  = u16 index[9] at offset 0x00
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

struct CTRPS2QuadBlockIndexPrefix
{
    u16 index[CTRPS2_LEVEL_QUADBLOCK_VERTEX_COUNT];
};

/*
 * Expand one current CTR high-LOD 3x3-grid face to a VIF-ready V3-16 stream.
 * This runtime gather is an integration baseline, not the final asset format.
 */
int CTRPS2_LevelBridgePackHighLodFaceV3_16(
    qword_t *dst,
    u32 dst_qwords,
    const struct CTRPS2QuadBlockIndexPrefix *block,
    const struct CTRPS2LevelVertexView *vertices,
    u32 vertex_count,
    u32 face_index);

/* Draw a source-layout fixture through the current M1 VIF1/VU1 path. */
int CTRPS2_LevelBridgeBenchRun(void);

#endif
