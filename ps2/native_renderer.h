#ifndef CTR_PS2_NATIVE_RENDERER_H
#define CTR_PS2_NATIVE_RENDERER_H

#include <tamtypes.h>

#define CTRPS2_FRAME_WIDTH   640
#define CTRPS2_FRAME_HEIGHT  448
#define CTRPS2_GS_ORIGIN_X   (2048 - (CTRPS2_FRAME_WIDTH / 2))
#define CTRPS2_GS_ORIGIN_Y   (2048 - (CTRPS2_FRAME_HEIGHT / 2))

#define CTRPS2_VU1_DB_BASE    8
#define CTRPS2_VU1_DB_OFFSET  496

int CTRPS2_NativeRendererInit(void);
void CTRPS2_NativeRendererClear(u8 r, u8 g, u8 b);
void CTRPS2_NativeRendererPresent(void);

#endif
