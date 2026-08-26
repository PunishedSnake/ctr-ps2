#include "native_geometry.h"
#include "native_renderer.h"

#include <dma.h>
#include <draw.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <packet2.h>
#include <packet2_chain.h>
#include <packet2_utils.h>
#include <packet2_vif.h>
#include <stdint.h>
#include <string.h>

#define CTRPS2_NATIVE_GEOMETRY_PROGRAM_ADDR       64
#define CTRPS2_NATIVE_GEOMETRY_HEADER_QWORDS      7
#define CTRPS2_NATIVE_GEOMETRY_VIF_QWORDS         96
#define CTRPS2_NATIVE_GEOMETRY_POSITION_DEST_QW   7
#define CTRPS2_NATIVE_GEOMETRY_OUTPUT_DEST_QW     96
#define CTRPS2_NATIVE_GEOMETRY_PACKED_NREG        3
#define CTRPS2_NATIVE_GEOMETRY_PACKED_REGLIST \
    (((u64)GIF_REG_ST) | ((u64)GIF_REG_RGBAQ << 4) | ((u64)GIF_REG_XYZ2 << 8))

/*
 * N1 keeps the validated three-stream input ABI:
 *
 * Input in one TOP/TOPS region:
 *   0              screen scale + vertex count
 *   1              screen offset
 *   2              primitive GIFtag
 *   3              ST normalization + Q seed
 *   4              reserved
 *   5..6           FINISH packet
 *   7..7+N          V3-16 positions
 *   next N          RGBA8
 *   next N          V4-16 source texcoords (12.4 texel U/V)
 * Output starts at TOP+96.
 */
#define CTRPS2_NATIVE_GEOMETRY_MAX_VERTICES \
    ((CTRPS2_NATIVE_GEOMETRY_OUTPUT_DEST_QW - CTRPS2_NATIVE_GEOMETRY_POSITION_DEST_QW) / 3u)

/* CURRENT IMPLEMENTATION: native resident material slot 0 is 64x64. */
#define CTRPS2_NATIVE_TEXTURE_WIDTH   64.0f
#define CTRPS2_NATIVE_TEXTURE_HEIGHT  64.0f

/*
 * N1b fixture depth contract.
 *
 * The prototype camera emits clip.z=1 and clip.w=view_z, so post-divide depth
 * is 1/view_z. GS opaque Z uses GEQUAL, therefore smaller view_z must produce
 * a larger stored value. FTOI4 multiplies by 16, hence a scale of
 * (65535 * near_z / 16) maps view_z=near_z to 0xffff for the 16-bit Z buffer.
 * The fixture never crosses near_z; the production camera will derive this
 * from its real near/far contract rather than these fixed constants.
 */
#define CTRPS2_NATIVE_DEPTH_NEAR_Z  8.0f
#define CTRPS2_NATIVE_DEPTH_MAX     65535.0f

extern u32 CTRPS2_VU1_NativeTrackStart __attribute__((section(".vudata")));
extern u32 CTRPS2_VU1_NativeTrackEnd __attribute__((section(".vudata")));

static qword_t s_header[CTRPS2_NATIVE_GEOMETRY_HEADER_QWORDS]
    __attribute__((aligned(64)));

static float s_objectToScreen[16] __attribute__((aligned(64))) = {
     1.0f,  0.0f, 0.0f, 0.0f,
     0.0f, -1.0f, 0.0f, 0.0f,
     0.0f,  0.0f, 0.0f, 1.0f,
     0.0f,  0.0f, 1.0f, 0.0f,
};

static packet2_t *s_vifPacket;
static struct CTRPS2NativeGeometryBatch s_batch;
static int s_initialized;
static int s_submitted;

static void CTRPS2_NativeGeometryWriteFloat(qword_t *qword, int component, float value)
{
    union
    {
        float f;
        u32 u;
    } bits;

    bits.f = value;
    qword->sw[component] = bits.u;
}

static int CTRPS2_NativeGeometryUploadProgram(void)
{
    packet2_t *upload;
    u32 qwords;

    qwords = packet2_utils_get_packet_size_for_program(
        &CTRPS2_VU1_NativeTrackStart,
        &CTRPS2_VU1_NativeTrackEnd) + 2u;

    upload = packet2_create((u16)qwords, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    if (upload == NULL)
        return 0;

    packet2_vif_add_micro_program(
        upload,
        CTRPS2_NATIVE_GEOMETRY_PROGRAM_ADDR,
        &CTRPS2_VU1_NativeTrackStart,
        &CTRPS2_VU1_NativeTrackEnd);
    packet2_utils_vu_add_end_tag(upload);

    dma_channel_send_packet2(upload, DMA_CHANNEL_VIF1, 1);
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    packet2_free(upload);
    return 1;
}

static int CTRPS2_NativeGeometryUploadMatrix(void)
{
    packet2_t *packet;

    packet = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    if (packet == NULL)
        return 0;

    packet2_utils_vu_add_unpack_data(
        packet,
        0,
        (void *)s_objectToScreen,
        4,
        0);
    packet2_utils_vu_add_end_tag(packet);

    dma_channel_send_packet2(packet, DMA_CHANNEL_VIF1, 1);
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    packet2_free(packet);
    return 1;
}

static int CTRPS2_NativeGeometryStreamValid(
    const void *ptr,
    u32 qwords,
    u32 required_bytes)
{
    if (ptr == NULL || qwords == 0)
        return 0;
    if (((uintptr_t)ptr & 15u) != 0)
        return 0;
    if (qwords > (0xffffffffu / 16u))
        return 0;
    return (qwords * 16u) >= required_bytes;
}

static int CTRPS2_NativeGeometryBatchValid(
    const struct CTRPS2NativeGeometryBatch *batch)
{
    u32 position_bytes;
    u32 color_bytes;
    u32 uv_bytes;

    if (batch == NULL)
        return 0;
    if (batch->vertex_count == 0 ||
        batch->vertex_count > CTRPS2_NATIVE_GEOMETRY_MAX_VERTICES)
        return 0;
    if (!batch->textured)
        return 0;

    position_bytes = (u32)batch->vertex_count * 3u * sizeof(s16);
    color_bytes = (u32)batch->vertex_count * 4u * sizeof(u8);
    uv_bytes = (u32)batch->vertex_count * 4u * sizeof(u16);

    if (!CTRPS2_NativeGeometryStreamValid(
            batch->positions_v3_16,
            batch->positions_qwords,
            position_bytes))
        return 0;
    if (!CTRPS2_NativeGeometryStreamValid(
            batch->colors_rgba8,
            batch->colors_qwords,
            color_bytes))
        return 0;
    if (!CTRPS2_NativeGeometryStreamValid(
            batch->uvs_v4_16,
            batch->uvs_qwords,
            uv_bytes))
        return 0;

    return 1;
}

static void CTRPS2_NativeGeometryBuildHeader(void)
{
    u64 prim;
    const float depth_scale =
        (CTRPS2_NATIVE_DEPTH_MAX * CTRPS2_NATIVE_DEPTH_NEAR_Z) / 16.0f;

    memset(s_header, 0, sizeof(s_header));

    CTRPS2_NativeGeometryWriteFloat(
        &s_header[0], 0, CTRPS2_FRAME_WIDTH * 0.5f);
    CTRPS2_NativeGeometryWriteFloat(
        &s_header[0], 1, CTRPS2_FRAME_HEIGHT * 0.5f);
    CTRPS2_NativeGeometryWriteFloat(&s_header[0], 2, depth_scale);
    s_header[0].sw[3] = s_batch.vertex_count;

    CTRPS2_NativeGeometryWriteFloat(
        &s_header[1],
        0,
        (float)(CTRPS2_GS_ORIGIN_X + (CTRPS2_FRAME_WIDTH / 2)));
    CTRPS2_NativeGeometryWriteFloat(
        &s_header[1],
        1,
        (float)(CTRPS2_GS_ORIGIN_Y + (CTRPS2_FRAME_HEIGHT / 2)));
    CTRPS2_NativeGeometryWriteFloat(&s_header[1], 2, 0.0f);

    /*
     * p2trk currently stores U/V in 12.4 texel space because that is the
     * already validated compact transport representation. STQ expects
     * normalized texture coordinates, so VU1 applies these per-material scale
     * factors after ITOF4. Slot 0 is 64x64 in N1. Later p2tex material data
     * supplies these values instead of compile-time constants.
     */
    CTRPS2_NativeGeometryWriteFloat(
        &s_header[3], 0, 1.0f / CTRPS2_NATIVE_TEXTURE_WIDTH);
    CTRPS2_NativeGeometryWriteFloat(
        &s_header[3], 1, 1.0f / CTRPS2_NATIVE_TEXTURE_HEIGHT);
    CTRPS2_NativeGeometryWriteFloat(&s_header[3], 2, 1.0f);

    /*
     * POTWIERDZONE/current PS2SDK: FST=0 selects floating STQ texture mapping.
     * The VU program emits ST before RGBAQ so GS Q state belongs to this vertex.
     */
    prim = GS_SET_PRIM(
        s_batch.gs_primitive,
        PRIM_SHADE_GOURAUD,
        DRAW_ENABLE,
        DRAW_DISABLE,
        DRAW_DISABLE,
        DRAW_DISABLE,
        PRIM_MAP_ST,
        0,
        PRIM_UNFIXED);

    s_header[2].dw[0] = VU_GS_GIFTAG(
        s_batch.vertex_count,
        0,
        1,
        prim,
        GIF_FLG_PACKED,
        CTRPS2_NATIVE_GEOMETRY_PACKED_NREG);
    s_header[2].dw[1] = CTRPS2_NATIVE_GEOMETRY_PACKED_REGLIST;

    s_header[5].dw[0] = GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1);
    s_header[5].dw[1] = GIF_REG_AD;
    s_header[6].dw[0] = 1;
    s_header[6].dw[1] = GS_REG_FINISH;
}

static void CTRPS2_NativeGeometryAddPositionUnpack(packet2_t *packet)
{
    static const u32 row[4] = {0, 0, 0, 0x3f800000u};
    Mask mask;

    mask.m = 0;
    mask.m3 = 1;
    mask.m7 = 1;
    mask.m11 = 1;
    mask.m15 = 1;

    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_strow(packet, row, 0);
    packet2_vif_nop(packet, 0);
    packet2_chain_close_tag(packet);

    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_stmask(packet, mask, 0);
    packet2_chain_close_tag(packet);

    packet2_chain_ref(
        packet,
        (void *)s_batch.positions_v3_16,
        s_batch.positions_qwords,
        0,
        0,
        0);
    packet2_vif_stcycl(packet, 0, 0x0101, 0);
    packet2_vif_open_unpack(
        packet,
        P2_UNPACK_V3_16,
        CTRPS2_NATIVE_GEOMETRY_POSITION_DEST_QW,
        1,
        1,
        0,
        0);
    packet2_vif_close_unpack_manual(packet, s_batch.vertex_count);
}

static void CTRPS2_NativeGeometryAddColorUnpack(packet2_t *packet)
{
    packet2_chain_ref(
        packet,
        (void *)s_batch.colors_rgba8,
        s_batch.colors_qwords,
        0,
        0,
        0);
    packet2_vif_stcycl(packet, 0, 0x0101, 0);
    packet2_vif_open_unpack(
        packet,
        P2_UNPACK_V4_8,
        CTRPS2_NATIVE_GEOMETRY_POSITION_DEST_QW + s_batch.vertex_count,
        1,
        0,
        1,
        0);
    packet2_vif_close_unpack_manual(packet, s_batch.vertex_count);
}

static void CTRPS2_NativeGeometryAddUVUnpack(packet2_t *packet)
{
    packet2_chain_ref(
        packet,
        (void *)s_batch.uvs_v4_16,
        s_batch.uvs_qwords,
        0,
        0,
        0);
    packet2_vif_stcycl(packet, 0, 0x0101, 0);
    packet2_vif_open_unpack(
        packet,
        P2_UNPACK_V4_16,
        CTRPS2_NATIVE_GEOMETRY_POSITION_DEST_QW +
            ((u32)s_batch.vertex_count * 2u),
        1,
        0,
        1,
        0);
    packet2_vif_close_unpack_manual(packet, s_batch.vertex_count);
}

static int CTRPS2_NativeGeometryBuildPacket(void)
{
    s_vifPacket = packet2_create(
        CTRPS2_NATIVE_GEOMETRY_VIF_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_CHAIN,
        1);
    if (s_vifPacket == NULL)
        return 0;

    packet2_utils_vu_add_unpack_data(
        s_vifPacket,
        0,
        s_header,
        CTRPS2_NATIVE_GEOMETRY_HEADER_QWORDS,
        1);

    CTRPS2_NativeGeometryAddPositionUnpack(s_vifPacket);
    CTRPS2_NativeGeometryAddColorUnpack(s_vifPacket);
    CTRPS2_NativeGeometryAddUVUnpack(s_vifPacket);

    /*
     * CURRENT IMPLEMENTATION correctness baseline. The helper's FLUSH+MSCAL
     * remains until N1c builds a multi-cluster chain and measures the narrower
     * dependency schedule on real hardware.
     */
    packet2_utils_vu_add_start_program(
        s_vifPacket,
        CTRPS2_NATIVE_GEOMETRY_PROGRAM_ADDR);
    packet2_utils_vu_add_end_tag(s_vifPacket);
    return 1;
}

int CTRPS2_NativeGeometryInit(void)
{
    if (!CTRPS2_NativeGeometryUploadProgram())
        return 0;
    if (!CTRPS2_NativeGeometryUploadMatrix())
        return 0;

    s_initialized = 1;
    s_submitted = 0;
    return 1;
}

int CTRPS2_NativeGeometrySetObjectToScreen(const float matrix[16])
{
    if (matrix == NULL || s_submitted)
        return 0;

    memcpy(s_objectToScreen, matrix, sizeof(s_objectToScreen));
    if (!s_initialized)
        return 1;
    return CTRPS2_NativeGeometryUploadMatrix();
}

int CTRPS2_NativeGeometryPrepare(
    const struct CTRPS2NativeGeometryBatch *batch)
{
    if (!s_initialized || s_submitted)
        return 0;
    if (!CTRPS2_NativeGeometryBatchValid(batch))
        return 0;

    s_batch = *batch;
    CTRPS2_NativeGeometryBuildHeader();

    if (s_vifPacket != NULL)
    {
        packet2_free(s_vifPacket);
        s_vifPacket = NULL;
    }

    return CTRPS2_NativeGeometryBuildPacket();
}

void CTRPS2_NativeGeometrySubmit(void)
{
    if (!s_initialized || s_vifPacket == NULL || s_submitted)
        return;

    dma_channel_send_packet2(s_vifPacket, DMA_CHANNEL_VIF1, 1);
    s_submitted = 1;
}

void CTRPS2_NativeGeometryWait(void)
{
    if (!s_submitted)
        return;

    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    draw_wait_finish();
    s_submitted = 0;
}
