#ifndef CTR_PS2_GEOMETRY_BENCH_H
#define CTR_PS2_GEOMETRY_BENCH_H

#include <tamtypes.h>

int CTRPS2_GeometryBenchInit(void);

/*
 * Rebuild the persistent VIF packet around a caller-provided signed V3-16
 * stream. This is startup/benchmark plumbing, not a per-draw allocation API.
 */
int CTRPS2_GeometryBenchConfigureV3_16(
    const void *positions_v3_16,
    u32 vertex_count,
    int gs_primitive);

/*
 * Same geometry path, but with one packed RGBA8 tuple per vertex. VIF1 expands
 * V4-8 directly to VU qwords, so the EE does not widen colors to 32-bit lanes.
 */
int CTRPS2_GeometryBenchConfigureV3_16_RGBA8(
    const void *positions_v3_16,
    const void *colors_rgba8,
    u32 vertex_count,
    int gs_primitive);

/*
 * M3 textured path. UV input is four unsigned 16-bit components per vertex;
 * only U/V are consumed by GS, while zero Z/W keep the VIF-expanded qword fully
 * deterministic. U/V use the GS 12.4 fixed-point convention expected by UV.
 */
int CTRPS2_GeometryBenchConfigureV3_16_RGBA8_UV16(
    const void *positions_v3_16,
    const void *colors_rgba8,
    const void *uvs_v4_16,
    u32 vertex_count,
    int gs_primitive);

void CTRPS2_GeometryBenchSubmit(void);
void CTRPS2_GeometryBenchWait(void);

#endif
