#ifndef CTR_PS2_NATIVE_TRACK_RENDERER_H
#define CTR_PS2_NATIVE_TRACK_RENDERER_H

#include <tamtypes.h>

int CTRPS2_NativeTrackRendererInit(const void *track_data, u32 track_bytes);
int CTRPS2_NativeTrackRendererDraw(void);

#endif
