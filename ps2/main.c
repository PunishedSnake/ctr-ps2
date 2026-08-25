#include "ctr_level_bridge.h"
#include "geometry_bench.h"
#include "renderer_ps2.h"

#include <kernel.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (!CTRPS2_RendererInit())
    {
        SleepThread();
        return 1;
    }

    /* M0: prove ready-made GIF transport through VIF1/VU1/XGKICK. */
    CTRPS2_RendererSubmitBootstrap();
    CTRPS2_RendererWaitForBootstrap();

    /*
     * M1: replace ready-made XYZ2 with signed V3-16 geometry. VIF1 expands it,
     * VU1 converts/transforms/projects it and produces the GS packet itself.
     */
    if (!CTRPS2_GeometryBenchInit())
    {
        SleepThread();
        return 2;
    }

    CTRPS2_GeometryBenchSubmit();
    CTRPS2_GeometryBenchWait();

    /* Leave the final frame showing only the level-geometry prototype. */
    CTRPS2_RendererClear(10, 14, 24);

    /*
     * M2b: consume the current CTR QuadBlock.index[9] + LevVertex layout,
     * gather all four ordinary high-LOD faces into one 22-vertex triangle strip,
     * stream packed RGBA8 colors beside V3-16 positions, and render the whole
     * block with one VIF1/VU1/XGKICK submission.
     */
    if (!CTRPS2_LevelBridgeBenchRun())
    {
        SleepThread();
        return 3;
    }

    for (;;)
        CTRPS2_RendererPresent();

    return 0;
}
