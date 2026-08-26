#ifndef CTR_PS2_NATIVE_TRACK_RENDERER_H
#define CTR_PS2_NATIVE_TRACK_RENDERER_H

#include <tamtypes.h>

int CTRPS2_NativeTrackRendererInit(const void *track_data, u32 track_bytes);

/* Submit/wait are split so frame scheduling can do unrelated EE work in between. */
int CTRPS2_NativeTrackRendererSubmit(void);
void CTRPS2_NativeTrackRendererWait(void);

/* Convenience correctness wrapper used by the current static prototype. */
int CTRPS2_NativeTrackRendererDraw(void);

#endif
