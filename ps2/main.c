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

    /* Submit as early as possible, then wait only at the correctness fence. */
    CTRPS2_RendererSubmitBootstrap();
    CTRPS2_RendererWaitForBootstrap();

    for (;;)
        CTRPS2_RendererPresent();

    return 0;
}
