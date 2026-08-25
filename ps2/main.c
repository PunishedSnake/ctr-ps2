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

    for (;;)
        CTRPS2_RendererPresent();

    return 0;
}
