#ifndef CTR_PS2_NATIVE_GEOMETRY_H
#define CTR_PS2_NATIVE_GEOMETRY_H

#include <tamtypes.h>

struct CTRPS2NativeGeometryBatch
{
    const void *positions_v3_16;
    u16 positions_qwords;

    const void *colors_rgba8;
    u16 colors_qwords;

    const void *uvs_16;
    u16 uvs_qwords;

    u16 vertex_count;
    u8 gs_primitive;
    u8 textured;
    u8 uv_v2_16;
    u8 reserved0;
};

int CTRPS2_NativeGeometryInit(void);
int CTRPS2_NativeGeometrySetObjectToScreen(const float matrix[16]);

int CTRPS2_NativeGeometryPassBegin(u32 batch_count);
int CTRPS2_NativeGeometryPassAppend(
    const struct CTRPS2NativeGeometryBatch *batch);
int CTRPS2_NativeGeometryPassEnd(void);

int CTRPS2_NativeGeometryPassSubmit(void);
void CTRPS2_NativeGeometryPassWait(void);

#endif
