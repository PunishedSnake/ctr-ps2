#ifndef CTR_PS2_RENDERER_H
#define CTR_PS2_RENDERER_H

int CTRPS2_RendererInit(void);
void CTRPS2_RendererSubmitBootstrap(void);
void CTRPS2_RendererWaitForBootstrap(void);
void CTRPS2_RendererPresent(void);

#endif
