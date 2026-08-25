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

    /*
     * Geometry init uploads the VU1 microprogram and shared matrix. It also
     * prepares the old M1 triangle packet, but the prototype intentionally does
     * not submit that packet anymore. M2b is now the only visible geometry draw.
     */
    if (!CTRPS2_GeometryBenchInit())
        CTRPS2_FailAfterRenderer(72, 12, 12);

    CTRPS2_RendererClear(10, 14, 24);

    /*
     * M2b: consume the current CTR QuadBlock.index[9] + LevVertex layout,
     * gather all four ordinary high-LOD faces into one 22-vertex triangle strip,
     * stream packed RGBA8 colors beside V3-16 positions, and render the whole
     * block with one VIF1/VU1/XGKICK submission.
     */
    if (!CTRPS2_LevelBridgeBenchRun())
        CTRPS2_FailAfterRenderer(72, 12, 72);

    for (;;)
        CTRPS2_RendererPresent();

    return 0;
}
