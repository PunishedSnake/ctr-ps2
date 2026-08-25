#include "ctr_texture_source.h"

#include <stddef.h>
#include <string.h>

#define CTRPS2_VRAM_HEADER_BYTES       0x14u
#define CTRPS2_VRAM_HEADER_RECT_OFFSET 0x0cu

#define CTRPS2_TPAGE_X_MASK       0x000fu
#define CTRPS2_TPAGE_Y_MASK       0x0010u
#define CTRPS2_TPAGE_BLEND_MASK   0x0060u
#define CTRPS2_TPAGE_COLOR_MASK   0x0180u
#define CTRPS2_TPAGE_BLEND_SHIFT  5u
#define CTRPS2_TPAGE_COLOR_SHIFT  7u

#define CTRPS2_CLUT_X_MASK        0x003fu
#define CTRPS2_CLUT_Y_MASK        0x7fc0u
#define CTRPS2_CLUT_Y_SHIFT       6u

#define CTRPS2_SOURCE_COLOR_4BIT  0u
#define CTRPS2_SOURCE_COLOR_8BIT  1u
#define CTRPS2_SOURCE_COLOR_15BIT 2u

static u16 s_sourceVram[CTRPS2_SOURCE_VRAM_WORD_COUNT]
    __attribute__((aligned(64)));

_Static_assert(sizeof(struct CTRPS2SourceTextureLayout) == 0x0c,
               "TextureLayout source view must remain 0x0c bytes");
_Static_assert(offsetof(struct CTRPS2SourceTextureLayout, u0) == 0x00,
               "TextureLayout.u0 offset changed");
_Static_assert(offsetof(struct CTRPS2SourceTextureLayout, clut) == 0x02,
               "TextureLayout.clut offset changed");
_Static_assert(offsetof(struct CTRPS2SourceTextureLayout, u1) == 0x04,
               "TextureLayout.u1 offset changed");
_Static_assert(offsetof(struct CTRPS2SourceTextureLayout, tpage) == 0x06,
               "TextureLayout.tpage offset changed");
_Static_assert(offsetof(struct CTRPS2SourceTextureLayout, u2) == 0x08,
               "TextureLayout.u2 offset changed");
_Static_assert(offsetof(struct CTRPS2SourceTextureLayout, u3) == 0x0a,
               "TextureLayout.u3 offset changed");

static u16 CTRPS2_ReadU16LE(const u8 *src)
{
    return (u16)((u16)src[0] | ((u16)src[1] << 8));
}

static u32 CTRPS2_ReadU32LE(const u8 *src)
{
    return (u32)src[0] |
           ((u32)src[1] << 8) |
           ((u32)src[2] << 16) |
           ((u32)src[3] << 24);
}

static void CTRPS2_WriteU16LE(u8 *dst, u16 value)
{
    dst[0] = (u8)value;
    dst[1] = (u8)(value >> 8);
}

static u32 CTRPS2_PackRGBA32(u8 r, u8 g, u8 b, u8 a)
{
    return (u32)r |
           ((u32)g << 8) |
           ((u32)b << 16) |
           ((u32)a << 24);
}

static u8 CTRPS2_Expand5To8(u32 value)
{
    value &= 0x1fu;
    return (u8)((value << 3) | (value >> 2));
}

static u16 CTRPS2_PackPSX1555(u8 r, u8 g, u8 b)
{
    return (u16)(((u16)(r >> 3) & 0x1fu) |
                 (((u16)(g >> 3) & 0x1fu) << 5) |
                 (((u16)(b >> 3) & 0x1fu) << 10));
}

static u32 CTRPS2_DecodePSX1555(u16 pixel)
{
    const u8 r = CTRPS2_Expand5To8(pixel);
    const u8 g = CTRPS2_Expand5To8(pixel >> 5);
    const u8 b = CTRPS2_Expand5To8(pixel >> 10);

    /*
     * Correctness/oracle alpha contract for the opaque M3 texture path:
     * PSX color 0 is transparent; every non-zero texel is made GS-opaque (0x80).
     * The PSX STP/semitransparency bit is intentionally not translated here.
     * Material/blend conversion is a separate source contract and must not be
     * silently folded into texture-color conversion.
     */
    const u8 a = ((pixel & 0x7fffu) == 0) ? 0x00u : 0x80u;
    return CTRPS2_PackRGBA32(r, g, b, a);
}

void CTRPS2_SourceVramClear(void)
{
    memset(s_sourceVram, 0, sizeof(s_sourceVram));
}

int CTRPS2_SourceVramReadWord(u16 *out_word, u32 x_word, u32 y)
{
    if (out_word == NULL)
        return 0;
    if (x_word >= CTRPS2_SOURCE_VRAM_WIDTH_WORDS ||
        y >= CTRPS2_SOURCE_VRAM_HEIGHT)
        return 0;

    *out_word = s_sourceVram[y * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + x_word];
    return 1;
}

static int CTRPS2_SourceVramApplyImage(const u8 *image, u32 bytes)
{
    u32 x;
    u32 y;
    u32 w;
    u32 h;
    u32 row;
    u32 required_bytes;
    const u8 *pixels;

    if (image == NULL || bytes < CTRPS2_VRAM_HEADER_BYTES)
        return 0;

    x = CTRPS2_ReadU16LE(image + CTRPS2_VRAM_HEADER_RECT_OFFSET + 0);
    y = CTRPS2_ReadU16LE(image + CTRPS2_VRAM_HEADER_RECT_OFFSET + 2);
    w = CTRPS2_ReadU16LE(image + CTRPS2_VRAM_HEADER_RECT_OFFSET + 4);
    h = CTRPS2_ReadU16LE(image + CTRPS2_VRAM_HEADER_RECT_OFFSET + 6);

    if (w == 0 || h == 0)
        return 0;
    if (x >= CTRPS2_SOURCE_VRAM_WIDTH_WORDS ||
        y >= CTRPS2_SOURCE_VRAM_HEIGHT)
        return 0;
    if (w > (CTRPS2_SOURCE_VRAM_WIDTH_WORDS - x) ||
        h > (CTRPS2_SOURCE_VRAM_HEIGHT - y))
        return 0;
    if (w > ((0xffffffffu - CTRPS2_VRAM_HEADER_BYTES) / 2u) / h)
        return 0;

    required_bytes = CTRPS2_VRAM_HEADER_BYTES + w * h * 2u;
    if (required_bytes > bytes)
        return 0;

    pixels = image + CTRPS2_VRAM_HEADER_BYTES;
    for (row = 0; row < h; ++row)
    {
        u16 *dst = &s_sourceVram[(y + row) * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + x];
        u32 column;

        for (column = 0; column < w; ++column)
            dst[column] = CTRPS2_ReadU16LE(pixels + ((row * w + column) * 2u));
    }

    return 1;
}

int CTRPS2_SourceVramApplyBlob(const void *blob, u32 bytes)
{
    const u8 *src = (const u8 *)blob;
    u32 copied = 0;

    if (src == NULL || bytes < CTRPS2_VRAM_HEADER_BYTES)
        return 0;

    /* Current LOAD_VramFileCallback(): first word 0x20 selects packed TIMs. */
    if (CTRPS2_ReadU32LE(src) != 0x20u)
        return CTRPS2_SourceVramApplyImage(src, bytes);

    {
        u32 cursor = 4u;

        while (cursor + 4u <= bytes)
        {
            const u32 size = CTRPS2_ReadU32LE(src + cursor);
            u32 step;

            cursor += 4u;
            if (size == 0)
                return (int)copied;

            /* Retail advances by (size & ~3). Preserve that contract safely. */
            step = size & ~3u;
            if (step < CTRPS2_VRAM_HEADER_BYTES)
                return 0;
            if (cursor > bytes || step > (bytes - cursor))
                return 0;
            if (!CTRPS2_SourceVramApplyImage(src + cursor, step))
                return 0;

            copied++;
            cursor += step;
        }
    }

    /* Packed form is expected to terminate with a zero size word. */
    return 0;
}

static void CTRPS2_MinMaxU8(u8 value, u8 *min_value, u8 *max_value)
{
    if (value < *min_value)
        *min_value = value;
    if (value > *max_value)
        *max_value = value;
}

int CTRPS2_SourceTextureDescribe(
    struct CTRPS2SourceTextureInfo *out,
    const struct CTRPS2SourceTextureLayout *layout)
{
    u8 min_u;
    u8 max_u;
    u8 min_v;
    u8 max_v;
    u32 color_mode;
    u32 max_source_x;
    u32 max_source_y;

    if (out == NULL || layout == NULL)
        return 0;

    min_u = max_u = layout->u0;
    min_v = max_v = layout->v0;
    CTRPS2_MinMaxU8(layout->u1, &min_u, &max_u);
    CTRPS2_MinMaxU8(layout->u2, &min_u, &max_u);
    CTRPS2_MinMaxU8(layout->u3, &min_u, &max_u);
    CTRPS2_MinMaxU8(layout->v1, &min_v, &max_v);
    CTRPS2_MinMaxU8(layout->v2, &min_v, &max_v);
    CTRPS2_MinMaxU8(layout->v3, &min_v, &max_v);

    color_mode = (layout->tpage & CTRPS2_TPAGE_COLOR_MASK) >> CTRPS2_TPAGE_COLOR_SHIFT;
    if (color_mode > CTRPS2_SOURCE_COLOR_15BIT)
        return 0;

    memset(out, 0, sizeof(*out));
    out->page_x_words = (u16)((layout->tpage & CTRPS2_TPAGE_X_MASK) * 64u);
    out->page_y = (layout->tpage & CTRPS2_TPAGE_Y_MASK) ? 256u : 0u;
    out->clut_x_words = (u16)((layout->clut & CTRPS2_CLUT_X_MASK) * 16u);
    out->clut_y = (u16)((layout->clut & CTRPS2_CLUT_Y_MASK) >> CTRPS2_CLUT_Y_SHIFT);
    out->width = (u16)((u32)max_u - min_u + 1u);
    out->height = (u16)((u32)max_v - min_v + 1u);
    out->min_u = min_u;
    out->min_v = min_v;
    out->color_mode = (u8)color_mode;
    out->semi_transparency = (u8)((layout->tpage & CTRPS2_TPAGE_BLEND_MASK) >> CTRPS2_TPAGE_BLEND_SHIFT);

    max_source_y = (u32)out->page_y + max_v;
    if (max_source_y >= CTRPS2_SOURCE_VRAM_HEIGHT)
        return 0;

    if (color_mode == CTRPS2_SOURCE_COLOR_4BIT)
        max_source_x = (u32)out->page_x_words + (max_u >> 2);
    else if (color_mode == CTRPS2_SOURCE_COLOR_8BIT)
        max_source_x = (u32)out->page_x_words + (max_u >> 1);
    else
        max_source_x = (u32)out->page_x_words + max_u;

    if (max_source_x >= CTRPS2_SOURCE_VRAM_WIDTH_WORDS)
        return 0;

    if (color_mode != CTRPS2_SOURCE_COLOR_15BIT)
    {
        const u32 palette_entries = (color_mode == CTRPS2_SOURCE_COLOR_4BIT) ? 16u : 256u;
        if (out->clut_y >= CTRPS2_SOURCE_VRAM_HEIGHT)
            return 0;
        if ((u32)out->clut_x_words + palette_entries > CTRPS2_SOURCE_VRAM_WIDTH_WORDS)
            return 0;
    }

    return 1;
}

static int CTRPS2_SourceTextureReadPixel(
    u16 *out_pixel,
    const struct CTRPS2SourceTextureInfo *info,
    u32 u,
    u32 v)
{
    u32 word_x;
    u32 word_y;
    u16 word;
    u32 index;

    if (out_pixel == NULL || info == NULL)
        return 0;

    word_y = (u32)info->page_y + v;
    if (word_y >= CTRPS2_SOURCE_VRAM_HEIGHT)
        return 0;

    if (info->color_mode == CTRPS2_SOURCE_COLOR_4BIT)
    {
        word_x = (u32)info->page_x_words + (u >> 2);
        if (!CTRPS2_SourceVramReadWord(&word, word_x, word_y))
            return 0;
        index = (word >> ((u & 3u) * 4u)) & 0x0fu;
        return CTRPS2_SourceVramReadWord(
            out_pixel,
            (u32)info->clut_x_words + index,
            info->clut_y);
    }

    if (info->color_mode == CTRPS2_SOURCE_COLOR_8BIT)
    {
        word_x = (u32)info->page_x_words + (u >> 1);
        if (!CTRPS2_SourceVramReadWord(&word, word_x, word_y))
            return 0;
        index = (word >> ((u & 1u) * 8u)) & 0xffu;
        return CTRPS2_SourceVramReadWord(
            out_pixel,
            (u32)info->clut_x_words + index,
            info->clut_y);
    }

    word_x = (u32)info->page_x_words + u;
    return CTRPS2_SourceVramReadWord(out_pixel, word_x, word_y);
}

int CTRPS2_SourceTextureDecodeRGBA32(
    u32 *dst_rgba32,
    u32 dst_pixel_capacity,
    u32 *out_width,
    u32 *out_height,
    const struct CTRPS2SourceTextureLayout *layout)
{
    struct CTRPS2SourceTextureInfo info;
    u32 x;
    u32 y;
    u32 pixel_count;

    if (dst_rgba32 == NULL || layout == NULL)
        return 0;
    if (!CTRPS2_SourceTextureDescribe(&info, layout))
        return 0;

    pixel_count = (u32)info.width * (u32)info.height;
    if (pixel_count > dst_pixel_capacity)
        return 0;

    for (y = 0; y < info.height; ++y)
    {
        for (x = 0; x < info.width; ++x)
        {
            u16 pixel;
            if (!CTRPS2_SourceTextureReadPixel(
                    &pixel,
                    &info,
                    (u32)info.min_u + x,
                    (u32)info.min_v + y))
                return 0;

            dst_rgba32[y * info.width + x] = CTRPS2_DecodePSX1555(pixel);
        }
    }

    if (out_width != NULL)
        *out_width = info.width;
    if (out_height != NULL)
        *out_height = info.height;
    return 1;
}

static void CTRPS2_SourceTextureWrite4Bit(u32 u, u32 v, u8 index)
{
    const u32 word_x = u >> 2;
    const u32 shift = (u & 3u) * 4u;
    u16 *word = &s_sourceVram[v * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + word_x];

    *word = (u16)((*word & ~(0x0fu << shift)) | (((u32)index & 0x0fu) << shift));
}

static void CTRPS2_SourceTextureWrite8Bit(u32 u, u32 v, u8 index)
{
    const u32 word_x = u >> 1;
    const u32 shift = (u & 1u) * 8u;
    u16 *word = &s_sourceVram[v * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + word_x];

    *word = (u16)((*word & ~(0xffu << shift)) | ((u32)index << shift));
}

static int CTRPS2_SourceTextureTestVramHeader(void)
{
    u8 blob[CTRPS2_VRAM_HEADER_BYTES + 8u];
    static const u16 expected[4] = {0x1111u, 0x2222u, 0x3333u, 0x4444u};
    u32 i;

    memset(blob, 0, sizeof(blob));
    CTRPS2_WriteU16LE(blob + CTRPS2_VRAM_HEADER_RECT_OFFSET + 0, 100u);
    CTRPS2_WriteU16LE(blob + CTRPS2_VRAM_HEADER_RECT_OFFSET + 2, 100u);
    CTRPS2_WriteU16LE(blob + CTRPS2_VRAM_HEADER_RECT_OFFSET + 4, 2u);
    CTRPS2_WriteU16LE(blob + CTRPS2_VRAM_HEADER_RECT_OFFSET + 6, 2u);
    for (i = 0; i < 4u; ++i)
        CTRPS2_WriteU16LE(blob + CTRPS2_VRAM_HEADER_BYTES + i * 2u, expected[i]);

    CTRPS2_SourceVramClear();
    if (CTRPS2_SourceVramApplyBlob(blob, sizeof(blob)) != 1)
        return 0;

    for (i = 0; i < 4u; ++i)
    {
        u16 actual;
        if (!CTRPS2_SourceVramReadWord(&actual, 100u + (i & 1u), 100u + (i >> 1)))
            return 0;
        if (actual != expected[i])
            return 0;
    }

    return 1;
}

int CTRPS2_SourceTextureSelfTest(void)
{
    struct CTRPS2SourceTextureLayout layout;
    u32 decoded[4];
    u32 width;
    u32 height;
    const u16 red = CTRPS2_PackPSX1555(0xf8, 0x00, 0x00);
    const u16 green = CTRPS2_PackPSX1555(0x00, 0xf8, 0x00);
    const u16 blue = CTRPS2_PackPSX1555(0x00, 0x00, 0xf8);
    const u16 white = CTRPS2_PackPSX1555(0xf8, 0xf8, 0xf8);

    if (!CTRPS2_SourceTextureTestVramHeader())
        return 0;

    /* 4-bit indexed */
    CTRPS2_SourceVramClear();
    s_sourceVram[500u * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + 0u] = red;
    s_sourceVram[500u * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + 1u] = green;
    s_sourceVram[500u * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + 2u] = blue;
    s_sourceVram[500u * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + 3u] = white;
    CTRPS2_SourceTextureWrite4Bit(0, 0, 0);
    CTRPS2_SourceTextureWrite4Bit(1, 0, 1);
    CTRPS2_SourceTextureWrite4Bit(0, 1, 2);
    CTRPS2_SourceTextureWrite4Bit(1, 1, 3);
    memset(&layout, 0, sizeof(layout));
    layout.u1 = 1;
    layout.v2 = 1;
    layout.u3 = 1;
    layout.v3 = 1;
    layout.clut = (u16)(500u << 6);
    if (!CTRPS2_SourceTextureDecodeRGBA32(decoded, 4, &width, &height, &layout))
        return 0;
    if (width != 2 || height != 2)
        return 0;
    if ((decoded[1] & 0x00ffffffu) != (CTRPS2_DecodePSX1555(green) & 0x00ffffffu) ||
        (decoded[2] & 0x00ffffffu) != (CTRPS2_DecodePSX1555(blue) & 0x00ffffffu))
        return 0;

    /* 8-bit indexed */
    CTRPS2_SourceVramClear();
    s_sourceVram[500u * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + 1u] = red;
    s_sourceVram[500u * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + 2u] = green;
    CTRPS2_SourceTextureWrite8Bit(0, 0, 1);
    CTRPS2_SourceTextureWrite8Bit(1, 0, 2);
    memset(&layout, 0, sizeof(layout));
    layout.u1 = 1;
    layout.u2 = 0;
    layout.u3 = 1;
    layout.clut = (u16)(500u << 6);
    layout.tpage = (u16)(CTRPS2_SOURCE_COLOR_8BIT << CTRPS2_TPAGE_COLOR_SHIFT);
    if (!CTRPS2_SourceTextureDecodeRGBA32(decoded, 4, &width, &height, &layout))
        return 0;
    if (width != 2 || height != 1)
        return 0;
    if ((decoded[0] & 0x00ffffffu) != (CTRPS2_DecodePSX1555(red) & 0x00ffffffu) ||
        (decoded[1] & 0x00ffffffu) != (CTRPS2_DecodePSX1555(green) & 0x00ffffffu))
        return 0;

    /* 15-bit direct */
    CTRPS2_SourceVramClear();
    s_sourceVram[0] = blue;
    s_sourceVram[1] = white;
    memset(&layout, 0, sizeof(layout));
    layout.u1 = 1;
    layout.u3 = 1;
    layout.tpage = (u16)(CTRPS2_SOURCE_COLOR_15BIT << CTRPS2_TPAGE_COLOR_SHIFT);
    if (!CTRPS2_SourceTextureDecodeRGBA32(decoded, 4, &width, &height, &layout))
        return 0;
    if (width != 2 || height != 1)
        return 0;
    if ((decoded[0] & 0x00ffffffu) != (CTRPS2_DecodePSX1555(blue) & 0x00ffffffu) ||
        (decoded[1] & 0x00ffffffu) != (CTRPS2_DecodePSX1555(white) & 0x00ffffffu))
        return 0;

    return 1;
}

int CTRPS2_SourceTextureBuildDiagnostic4Bit(
    u32 *dst_rgba32,
    u32 dst_pixel_capacity,
    struct CTRPS2SourceTextureLayout *out_layout)
{
    static const u16 palette[16] = {
        0,
        0x18bau, /* red-ish bright */
        0x0c57u, /* red-ish dark */
        0x2645u, /* green-ish bright */
        0x1903u, /* green-ish dark */
        0x6d05u, /* blue-ish bright */
        0x48c4u, /* blue-ish dark */
        0x16fbu, /* yellow-ish bright */
        0x0db7u, /* yellow-ish dark */
        0x7fffu, /* white */
        0x7d5fu, /* magenta */
        0x4210u,
        0x294au,
        0x56b5u,
        0x2108u,
        0x6318u,
    };
    struct CTRPS2SourceTextureLayout layout;
    u32 x;
    u32 y;
    u32 width;
    u32 height;

    if (dst_rgba32 == NULL || out_layout == NULL)
        return 0;
    if (dst_pixel_capacity < 64u * 64u)
        return 0;

    CTRPS2_SourceVramClear();

    for (x = 0; x < 16u; ++x)
        s_sourceVram[400u * CTRPS2_SOURCE_VRAM_WIDTH_WORDS + x] = palette[x];

    for (y = 0; y < 64u; ++y)
    {
        for (x = 0; x < 64u; ++x)
        {
            const int checker = ((x >> 3) ^ (y >> 3)) & 1;
            u8 index;

            if (y < 32u)
                index = (u8)((x < 32u) ? (checker ? 2 : 1) : (checker ? 4 : 3));
            else
                index = (u8)((x < 32u) ? (checker ? 6 : 5) : (checker ? 8 : 7));

            if (x == y)
                index = 9;
            else if ((x + y) == 63u)
                index = 10;

            if (x < 2u || y < 2u || x >= 62u || y >= 62u)
                index = 9;

            CTRPS2_SourceTextureWrite4Bit(x, y, index);
        }
    }

    memset(&layout, 0, sizeof(layout));
    layout.u0 = 0;
    layout.v0 = 0;
    layout.u1 = 63;
    layout.v1 = 0;
    layout.u2 = 0;
    layout.v2 = 63;
    layout.u3 = 63;
    layout.v3 = 63;
    layout.clut = (u16)(400u << CTRPS2_CLUT_Y_SHIFT);
    layout.tpage = 0; /* page (0,0), 4-bit, opaque blend field */

    if (!CTRPS2_SourceTextureDecodeRGBA32(
            dst_rgba32,
            dst_pixel_capacity,
            &width,
            &height,
            &layout))
        return 0;
    if (width != 64u || height != 64u)
        return 0;

    *out_layout = layout;
    return 1;
}
