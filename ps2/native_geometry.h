#ifndef CTR_PS2_NATIVE_GEOMETRY_H
#define CTR_PS2_NATIVE_GEOMETRY_H

#include <tamtypes.h>

struct CTRPS2NativeGeometryBatch
{
    const void *positions_v3_16;
    u16 positions_qwords;

    const void *colors_rgba8;
    u16 colors_qwords;

    const void *uvs_v4_16;
    u16 uvs_qwords;

    u16 vertex_count;
    u8 gs_primitive;
    u8 textured;
};

int CTRPS2_NativeGeometryInit(void);
int CTRPS2_NativeGeometrySetObjectToScreen(const float matrix[16]);

/*
 * Build one persistent VIF1 chain that REFers directly into a PS2-ready asset.
 * No vertex gather/repack/copy is performed. The source memory must stay alive
 * and unmodified until CTRPS2_NativeGeometryWait() retires the submission.
 */
int CTRPS2_NativeGeometryPrepare(
    const struct CTRPS2NativeGeometryBatch *batch);

void CTRPS2_NativeGeometrySubmit(void);
void CTRPS2_NativeGeometryWait(void);

#endif
