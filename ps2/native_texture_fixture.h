#ifndef CTR_PS2_NATIVE_TEXTURE_FIXTURE_H
#define CTR_PS2_NATIVE_TEXTURE_FIXTURE_H

#include <tamtypes.h>

#define CTRPS2_NATIVE_TEXTURE_WIDTH       64u
#define CTRPS2_NATIVE_TEXTURE_HEIGHT      64u
#define CTRPS2_NATIVE_TEXTURE_4BIT_BYTES  \
    ((CTRPS2_NATIVE_TEXTURE_WIDTH * CTRPS2_NATIVE_TEXTURE_HEIGHT) / 2u)
#define CTRPS2_NATIVE_TEXTURE_CLUT_ENTRIES 16u

int CTRPS2_NativeTextureFixtureBuild(
    u8 *indices_psmt4,
    u32 index_capacity,
    u32 *clut_rgba32,
    u32 clut_capacity);

#endif
