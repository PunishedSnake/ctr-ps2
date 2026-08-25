#include "ctr_level_bridge.h"
#include "geometry_bench.h"
#include "renderer_ps2.h"

#include <kernel.h>

static void CTRPS2_FailAfterRenderer(u8 r, u8 g, u8 b)
{
    CTRPS2_RendererClear(r, g, b);
    for (;;)
        CTRPS2_RendererPresent();
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (!CTRPS2_RendererInit())
    {
        SleepThread();
        return 1;
    }

    /* Upload only the M3 geometry microprogram and shared transform matrix. */
    if (!CTRPS2_GeometryBenchInit())
        CTRPS2_FailAfterRenderer(72, 12, 12);

    CTRPS2_RendererClear(10, 14, 24);

    /*
     * M3a: preserve the real-hardware validated 22-vertex QuadBlock strip, add
     * packed UV through VIF1/VU1, and sample one resident asymmetric 64x64 GS
     * texture on each of the four ordinary high-LOD faces. This proves the PS2
     * texture transport/state/attribute path before retail CTR texture conversion
     * and uv_rotation semantics are allowed onto the critical path.
     */
    if (!CTRPS2_LevelBridgeBenchRun())
        CTRPS2_FailAfterRenderer(72, 12, 72);

    for (;;)
        CTRPS2_RendererPresent();

    return 0;
}
