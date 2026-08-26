#ifndef CTR_PS2_TEXTURE_PACK_H
#define CTR_PS2_TEXTURE_PACK_H

#include "ctr_texture_source.h"

#include <tamtypes.h>

#define CTRPS2_TEXTURE_PACK_MAX_4BIT_PIXELS (256u * 256u)
#define CTRPS2_TEXTURE_PACK_MAX_4BIT_BYTES  (CTRPS2_TEXTURE_PACK_MAX_4BIT_PIXELS / 2u)
#define CTRPS2_TEXTURE_PACK_4BIT_CLUT_ENTRIES 16u

/*
 * Convert a retail 4-bit TextureLayout into a row-major packed index payload
 * suitable for a GS PSMT4 image transfer. No color widening happens here.
 */
int CTRPS2_TexturePack4BitIndices(
    u8 *dst_indices,
    u32 dst_byte_capacity,
    u32 *out_width,
    u32 *out_height,
    u32 *out_bytes,
    const struct CTRPS2SourceTextureLayout *layout);

/* Convert the retail 16-entry PSX CLUT into a GS RGBA32 CLUT. */
int CTRPS2_TexturePack4BitClutRGBA32(
    u32 *dst_clut_rgba32,
    u32 dst_entry_capacity,
    const struct CTRPS2SourceTextureLayout *layout);

/*
 * M3d diagnostic builder. It creates the same retail-shaped 4-bit source used
 * by M3c, then validates direct PSMT4+CLUT packing against the RGBA oracle.
 */
int CTRPS2_TexturePackBuildDiagnosticPSMT4(
    u8 *dst_indices,
    u32 dst_byte_capacity,
    u32 *dst_clut_rgba32,
    u32 dst_clut_entry_capacity,
    u32 *out_width,
    u32 *out_height,
    struct CTRPS2SourceTextureLayout *out_layout);

#endif
