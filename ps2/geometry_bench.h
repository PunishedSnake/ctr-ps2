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

void CTRPS2_GeometryBenchSubmit(void);
void CTRPS2_GeometryBenchWait(void);

#endif
