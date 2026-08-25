#include "ctr_texture_pack.h"

#include <string.h>

#define CTRPS2_SOURCE_COLOR_4BIT 0u

static u32 s_rgbaOracleScratch[64u * 64u]
    __attribute__((aligned(64)));

static u8 CTRPS2_TexturePackExpand5To8(u32 value)
{
    value &= 0x1fu;
    return (u8)((value << 3) | (value >> 2));
}

static u32 CTRPS2_TexturePackDecodePSX1555(u16 pixel)
{
    const u8 r = CTRPS2_TexturePackExpand5To8(pixel);
    const u8 g = CTRPS2_TexturePackExpand5To8(pixel >> 5);
    const u8 b = CTRPS2_TexturePackExpand5To8(pixel >> 10);
    const u8 a = ((pixel & 0x7fffu) == 0) ? 0x00u : 0x80u;

    return (u32)r |
           ((u32)g << 8) |
           ((u32)b << 16) |
           ((u32)a << 24);
}

static int CTRPS2_TexturePackRead4BitIndex(
    u8 *out_index,
    const struct CTRPS2SourceTextureInfo *info,
    u32 u,
    u32 v)
{
    u16 word;
    const u32 x_word = (u32)info->page_x_words + (u >> 2);
    const u32 y = (u32)info->page_y + v;

    if (out_index == NULL || info == NULL)
        return 0;
    if (!CTRPS2_SourceVramReadWord(&word, x_word, y))
        return 0;

    *out_index = (u8)((word >> ((u & 3u) * 4u)) & 0x0fu);
    return 1;
}

int CTRPS2_TexturePack4BitIndices(
    u8 *dst_indices,
    u32 dst_byte_capacity,
    u32 *out_width,
    u32 *out_height,
    u32 *out_bytes,
    const struct CTRPS2SourceTextureLayout *layout)
{
    struct CTRPS2SourceTextureInfo info;
    u32 pixel_count;
    u32 byte_count;
    u32 x;
    u32 y;

    if (dst_indices == NULL || layout == NULL)
        return 0;
    if (!CTRPS2_SourceTextureDescribe(&info, layout))
        return 0;
    if (info.color_mode != CTRPS2_SOURCE_COLOR_4BIT)
        return 0;

    pixel_count = (u32)info.width * (u32)info.height;
    byte_count = (pixel_count + 1u) >> 1;
    if (byte_count > dst_byte_capacity)
        return 0;

    memset(dst_indices, 0, byte_count);

    for (y = 0; y < info.height; ++y)
    {
        for (x = 0; x < info.width; ++x)
        {
            const u32 pixel_index = y * (u32)info.width + x;
            const u32 dst_byte = pixel_index >> 1;
            const u32 shift = (pixel_index & 1u) * 4u;
            u8 source_index;

            if (!CTRPS2_TexturePackRead4BitIndex(
                    &source_index,
                    &info,
                    (u32)info.min_u + x,
                    (u32)info.min_v + y))
                return 0;

            dst_indices[dst_byte] = (u8)(dst_indices[dst_byte] |
                                         (u8)(source_index << shift));
        }
    }

    if (out_width != NULL)
        *out_width = info.width;
    if (out_height != NULL)
        *out_height = info.height;
    if (out_bytes != NULL)
        *out_bytes = byte_count;
    return 1;
}

int CTRPS2_TexturePack4BitClutRGBA32(
    u32 *dst_clut_rgba32,
    u32 dst_entry_capacity,
    const struct CTRPS2SourceTextureLayout *layout)
{
    struct CTRPS2SourceTextureInfo info;
    u32 i;

    if (dst_clut_rgba32 == NULL || layout == NULL)
        return 0;
    if (dst_entry_capacity < CTRPS2_TEXTURE_PACK_4BIT_CLUT_ENTRIES)
        return 0;
    if (!CTRPS2_SourceTextureDescribe(&info, layout))
        return 0;
    if (info.color_mode != CTRPS2_SOURCE_COLOR_4BIT)
        return 0;

    for (i = 0; i < CTRPS2_TEXTURE_PACK_4BIT_CLUT_ENTRIES; ++i)
    {
        u16 source_color;
        if (!CTRPS2_SourceVramReadWord(
                &source_color,
                (u32)info.clut_x_words + i,
                info.clut_y))
            return 0;

        dst_clut_rgba32[i] = CTRPS2_TexturePackDecodePSX1555(source_color);
    }

    return 1;
}

int CTRPS2_TexturePackBuildDiagnosticPSMT4(
    u8 *dst_indices,
    u32 dst_byte_capacity,
    u32 *dst_clut_rgba32,
    u32 dst_clut_entry_capacity,
    u32 *out_width,
    u32 *out_height,
    struct CTRPS2SourceTextureLayout *out_layout)
{
    struct CTRPS2SourceTextureLayout layout;
    u32 width;
    u32 height;
    u32 packed_bytes;
    u32 pixel_count;
    u32 i;

    if (dst_indices == NULL || dst_clut_rgba32 == NULL || out_layout == NULL)
        return 0;

    /*
     * This fixture helper intentionally asks M3c for the RGBA oracle as well as
     * the retail-shaped source data. Real assets will enter through VRM +
     * TextureLayout and call the direct index/CLUT packers without this decode.
     */
    if (!CTRPS2_SourceTextureBuildDiagnostic4Bit(
            s_rgbaOracleScratch,
            64u * 64u,
            &layout))
        return 0;

    if (!CTRPS2_TexturePack4BitIndices(
            dst_indices,
            dst_byte_capacity,
            &width,
            &height,
            &packed_bytes,
            &layout))
        return 0;
    if (!CTRPS2_TexturePack4BitClutRGBA32(
            dst_clut_rgba32,
            dst_clut_entry_capacity,
            &layout))
        return 0;

    if (width != 64u || height != 64u)
        return 0;
    if (packed_bytes != (64u * 64u) / 2u)
        return 0;

    /*
     * Byte-for-byte semantic oracle: reconstruct every RGBA texel from the
     * direct packed index stream + GS CLUT and compare to the M3c decode.
     */
    pixel_count = width * height;
    for (i = 0; i < pixel_count; ++i)
    {
        const u8 packed = dst_indices[i >> 1];
        const u8 index = (u8)((packed >> ((i & 1u) * 4u)) & 0x0fu);
        if (dst_clut_rgba32[index] != s_rgbaOracleScratch[i])
            return 0;
    }

    if (out_width != NULL)
        *out_width = width;
    if (out_height != NULL)
        *out_height = height;
    *out_layout = layout;
    return 1;
}
