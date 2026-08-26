#ifndef CTR_PS2_GEOMETRY_BENCH_H
#define CTR_PS2_GEOMETRY_BENCH_H

#include <tamtypes.h>

int CTRPS2_GeometryBenchInit(void);

/*
 * Upload a new object/view/projection matrix into the shared VU1 constant area.
 * The matrix is column-major because the VU1 microprogram consumes four columns.
 */
int CTRPS2_GeometryBenchSetObjectToScreen(const float matrix[16]);

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
 * M3 compatibility/oracle path. Input is copied into persistent qword-padded
 * scratch buffers before the VIF chain is built.
 */
int CTRPS2_GeometryBenchConfigureV3_16_RGBA8_UV16(
    const void *positions_v3_16,
    const void *colors_rgba8,
    const void *uvs_v4_16,
    u32 vertex_count,
    int gs_primitive);

/*
 * Native static-asset path. The packet REF tags point directly at immutable,
 * qword-padded PS2-ready streams. No gather, widening or memcpy is performed.
 *
 * Each source pointer must be 16-byte DMA aligned and each qword count must
 * cover the logical vertex payload. The asset remains owned by the caller until
 * CTRPS2_GeometryBenchWait() retires the transfer.
 *
 * This function lives in geometry_bench temporarily so the already validated
 * VU1 microprogram/submission path can be reused while the shipping renderer is
 * brought up. The final renderer will own this core API directly.
 */
int CTRPS2_GeometryBenchConfigureRefsV3_16_RGBA8_UV16(
    const void *positions_v3_16,
    u32 position_qwords,
    const void *colors_rgba8,
    u32 color_qwords,
    const void *uvs_v4_16,
    u32 uv_qwords,
    u32 vertex_count,
    int gs_primitive);

void CTRPS2_GeometryBenchSubmit(void);
void CTRPS2_GeometryBenchWait(void);

#endif
