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
 * N1c pass builder.
 *
 * Begin allocates one persistent VIF1 DMA chain for the entire pass. Append
 * records each immutable p2trk batch as REF/UNPACK commands and alternates the
 * VIF1 TOP/TOPS ownership through MSCAL. End seals the chain. No vertex gather,
 * packet rebuild or heap allocation happens during subsequent frame submits.
 */
int CTRPS2_NativeGeometryPassBegin(u32 batch_count);
int CTRPS2_NativeGeometryPassAppend(
    const struct CTRPS2NativeGeometryBatch *batch);
int CTRPS2_NativeGeometryPassEnd(void);

/*
 * Submit starts the whole persistent opaque pass. Wait is deliberately separate
 * so the caller can schedule unrelated EE work before the single late fence.
 */
int CTRPS2_NativeGeometryPassSubmit(void);
void CTRPS2_NativeGeometryPassWait(void);

#endif
