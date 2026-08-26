#ifndef CTR_PS2_TEXTURE_SOURCE_H
#define CTR_PS2_TEXTURE_SOURCE_H

#include <tamtypes.h>

#define CTRPS2_SOURCE_VRAM_WIDTH_WORDS  1024u
#define CTRPS2_SOURCE_VRAM_HEIGHT       512u
#define CTRPS2_SOURCE_VRAM_WORD_COUNT \
    (CTRPS2_SOURCE_VRAM_WIDTH_WORDS * CTRPS2_SOURCE_VRAM_HEIGHT)

#define CTRPS2_SOURCE_TEXTURE_MAX_UV_DIMENSION 256u

/*
 * PS2-owned read-only view of current ctr-native TextureLayout.
 * Source truth: include/namespace_Decal.h, sizeof(TextureLayout) == 0x0c.
 * Keep this independent from PS1/PsyQ headers so the PS2 asset boundary stays
 * explicit and can later move into an offline packer without ABI leakage.
 */
struct CTRPS2SourceTextureLayout
{
    u8 u0;
    u8 v0;
    u16 clut;

    u8 u1;
    u8 v1;
    u16 tpage;

    u8 u2;
    u8 v2;
    u8 u3;
    u8 v3;
};

struct CTRPS2SourceTextureInfo
{
    u16 page_x_words;
    u16 page_y;
    u16 clut_x_words;
    u16 clut_y;
    u16 width;
    u16 height;
    u8 min_u;
    u8 min_v;
    u8 color_mode;
    u8 semi_transparency;
};

/* Reset the 1024x512 retail-shaped source mirror used only during conversion. */
void CTRPS2_SourceVramClear(void);

/*
 * Parse a retail VRM payload and apply every VramHeader rectangle to the source
 * mirror. Supports the single-image and packed-multiple-image forms used by
 * current LOAD_VramFileCallback(). Returns the number of copied rectangles, or
 * zero on malformed input.
 */
int CTRPS2_SourceVramApplyBlob(const void *blob, u32 bytes);

/* Return a read-only word from the conversion mirror for diagnostics/tests. */
int CTRPS2_SourceVramReadWord(u16 *out_word, u32 x_word, u32 y);

/* Decode source address/size metadata without touching pixel storage. */
int CTRPS2_SourceTextureDescribe(
    struct CTRPS2SourceTextureInfo *out,
    const struct CTRPS2SourceTextureLayout *layout);

/*
 * Correctness/oracle path: decode one retail TextureLayout from the source PS1
 * VRAM representation to row-major RGBA32. This is deliberately NOT the final
 * runtime texture representation. Shipping assets should remain indexed when
 * PSMT4/PSMT8 + CLUT is the better GS consumer format.
 */
int CTRPS2_SourceTextureDecodeRGBA32(
    u32 *dst_rgba32,
    u32 dst_pixel_capacity,
    u32 *out_width,
    u32 *out_height,
    const struct CTRPS2SourceTextureLayout *layout);

/* Pure source-format checks plus a 64x64 4-bit diagnostic texture generator. */
int CTRPS2_SourceTextureSelfTest(void);
int CTRPS2_SourceTextureBuildDiagnostic4Bit(
    u32 *dst_rgba32,
    u32 dst_pixel_capacity,
    struct CTRPS2SourceTextureLayout *out_layout);

#endif
