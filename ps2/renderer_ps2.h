#ifndef CTR_PS2_RENDERER_H
#define CTR_PS2_RENDERER_H

#define CTRPS2_FRAME_WIDTH   640
#define CTRPS2_FRAME_HEIGHT  448
#define CTRPS2_GS_ORIGIN_X   (2048 - (CTRPS2_FRAME_WIDTH / 2))
#define CTRPS2_GS_ORIGIN_Y   (2048 - (CTRPS2_FRAME_HEIGHT / 2))

/*
 * QW 0..7 are shared VU1 constants. Two 496-QW TOP/TOPS regions occupy the
 * remainder of the 1024-QW VU1 data memory: A starts at 8, B at 504.
 */
#define CTRPS2_VU1_DB_BASE    8
#define CTRPS2_VU1_DB_OFFSET  496

int CTRPS2_RendererInit(void);
void CTRPS2_RendererSubmitBootstrap(void);
void CTRPS2_RendererWaitForBootstrap(void);
void CTRPS2_RendererPresent(void);

#endif
