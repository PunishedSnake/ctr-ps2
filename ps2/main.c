#include "native_renderer.h"
#include "native_track_fixture.h"
#include "native_track_renderer.h"

#include <kernel.h>

static void CTRPS2_FailAfterRenderer(u8 r, u8 g, u8 b)
{
    CTRPS2_NativeRendererClear(r, g, b);
    for (;;)
        CTRPS2_NativeRendererPresent();
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (!CTRPS2_NativeRendererInit())
    {
        SleepThread();
        return 1;
    }

    /*
     * Native N0: default runtime no longer constructs PS1 primitives, traverses
     * QuadBlock render fields or decodes TextureLayout/VRM. It opens one p2trk
     * memory image whose streams are already VIF-ready and DMA aligned.
     */
    if (!CTRPS2_NativeTrackRendererInit(
            CTRPS2_NativeTrackFixtureData(),
            CTRPS2_NativeTrackFixtureBytes()))
        CTRPS2_FailAfterRenderer(72, 12, 12);

    CTRPS2_NativeRendererClear(8, 12, 22);

    if (!CTRPS2_NativeTrackRendererDraw())
        CTRPS2_FailAfterRenderer(72, 12, 72);

    for (;;)
        CTRPS2_NativeRendererPresent();

    return 0;
}
