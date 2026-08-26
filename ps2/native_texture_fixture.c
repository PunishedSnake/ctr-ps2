#include "native_texture_fixture.h"

#include <string.h>

static u32 CTRPS2_NativeTextureRGBA(u8 r, u8 g, u8 b)
{
    return (u32)r |
           ((u32)g << 8) |
           ((u32)b << 16) |
           ((u32)0x80 << 24);
}

static u8 CTRPS2_NativeTextureBaseIndex(u32 x, u32 y)
{
    const int checker = ((x >> 3) ^ (y >> 3)) & 1;
    u8 index;

    if (y < (CTRPS2_NATIVE_TEXTURE_HEIGHT / 2u))
    {
        index = (x < (CTRPS2_NATIVE_TEXTURE_WIDTH / 2u)) ? 1u : 3u;
    }
    else
    {
        index = (x < (CTRPS2_NATIVE_TEXTURE_WIDTH / 2u)) ? 5u : 7u;
    }

    if (checker)
        index++;

    if (x == y)
        index = 9u;
    else if ((x + y) == (CTRPS2_NATIVE_TEXTURE_WIDTH - 1u))
        index = 10u;

    if (x < 2u || y < 2u ||
        x >= (CTRPS2_NATIVE_TEXTURE_WIDTH - 2u) ||
        y >= (CTRPS2_NATIVE_TEXTURE_HEIGHT - 2u))
        index = 9u;

    return index;
}

int CTRPS2_NativeTextureFixtureBuild(
    u8 *indices_psmt4,
    u32 index_capacity,
    u32 *clut_rgba32,
    u32 clut_capacity)
{
    u32 x;
    u32 y;

    if (indices_psmt4 == NULL || clut_rgba32 == NULL)
        return 0;
    if (index_capacity < CTRPS2_NATIVE_TEXTURE_4BIT_BYTES)
        return 0;
    if (clut_capacity < CTRPS2_NATIVE_TEXTURE_CLUT_ENTRIES)
        return 0;

    memset(indices_psmt4, 0, CTRPS2_NATIVE_TEXTURE_4BIT_BYTES);
    memset(clut_rgba32, 0,
           CTRPS2_NATIVE_TEXTURE_CLUT_ENTRIES * sizeof(u32));

    clut_rgba32[0]  = 0;
    clut_rgba32[1]  = CTRPS2_NativeTextureRGBA(0xd0, 0x28, 0x38);
    clut_rgba32[2]  = CTRPS2_NativeTextureRGBA(0x9c, 0x1e, 0x2a);
    clut_rgba32[3]  = CTRPS2_NativeTextureRGBA(0x28, 0xc8, 0x48);
    clut_rgba32[4]  = CTRPS2_NativeTextureRGBA(0x1e, 0x96, 0x36);
    clut_rgba32[5]  = CTRPS2_NativeTextureRGBA(0x28, 0x48, 0xd8);
    clut_rgba32[6]  = CTRPS2_NativeTextureRGBA(0x1e, 0x36, 0xa2);
    clut_rgba32[7]  = CTRPS2_NativeTextureRGBA(0xd8, 0xb8, 0x28);
    clut_rgba32[8]  = CTRPS2_NativeTextureRGBA(0xa2, 0x8a, 0x1e);
    clut_rgba32[9]  = CTRPS2_NativeTextureRGBA(0xf0, 0xf0, 0xf0);
    clut_rgba32[10] = CTRPS2_NativeTextureRGBA(0xff, 0x50, 0xff);

    for (y = 0; y < CTRPS2_NATIVE_TEXTURE_HEIGHT; ++y)
    {
        for (x = 0; x < CTRPS2_NATIVE_TEXTURE_WIDTH; ++x)
        {
            const u8 index = CTRPS2_NativeTextureBaseIndex(x, y);
            const u32 pixel = y * CTRPS2_NATIVE_TEXTURE_WIDTH + x;
            u8 *dst = &indices_psmt4[pixel >> 1];

            if ((pixel & 1u) == 0)
                *dst = (u8)((*dst & 0xf0u) | index);
            else
                *dst = (u8)((*dst & 0x0fu) | (index << 4));
        }
    }

    return 1;
}
