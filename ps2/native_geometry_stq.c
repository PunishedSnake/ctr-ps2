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
#define CTRPS2_NATIVE_GEOMETRY_POSITION_DEST_QW   7
#define CTRPS2_NATIVE_GEOMETRY_OUTPUT_DEST_QW     96
#define CTRPS2_NATIVE_GEOMETRY_PACKED_NREG        3
#define CTRPS2_NATIVE_GEOMETRY_PACKED_REGLIST \
    (((u64)GIF_REG_ST) | ((u64)GIF_REG_RGBAQ << 4) | ((u64)GIF_REG_XYZ2 << 8))

#define CTRPS2_NATIVE_GEOMETRY_PACKET_BASE_QWORDS       8u
#define CTRPS2_NATIVE_GEOMETRY_PACKET_QWORDS_PER_BATCH 48u

#ifndef CTRPS2_NATIVE_VIF_REUSE_AWARE
#define CTRPS2_NATIVE_VIF_REUSE_AWARE 0
#endif

#define CTRPS2_NATIVE_GEOMETRY_MAX_VERTICES \
    ((CTRPS2_NATIVE_GEOMETRY_OUTPUT_DEST_QW - CTRPS2_NATIVE_GEOMETRY_POSITION_DEST_QW) / 3u)

#define CTRPS2_NATIVE_DEPTH_NEAR_Z  8.0f
#define CTRPS2_NATIVE_DEPTH_MAX     65535.0f

extern u32 CTRPS2_VU1_NativeTrackStart __attribute__((section(".vudata")));
extern u32 CTRPS2_VU1_NativeTrackEnd __attribute__((section(".vudata")));

static float s_objectToScreen[16] __attribute__((aligned(64))) = {
     1.0f,  0.0f, 0.0f, 0.0f,
     0.0f, -1.0f, 0.0f, 0.0f,
     0.0f,  0.0f, 0.0f, 1.0f,
     0.0f,  0.0f, 1.0f, 0.0f,
};

static packet2_t *s_vifPacket;
static u32 s_expectedBatches;
static u32 s_appendedBatches;
static int s_initialized;
static int s_passReady;
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
    u32 uv_lanes;

    if (batch == NULL)
        return 0;
    if (batch->vertex_count == 0 ||
        batch->vertex_count > CTRPS2_NATIVE_GEOMETRY_MAX_VERTICES)
        return 0;
    if (!batch->textured)
        return 0;
    if (batch->texture_width == 0 || batch->texture_height == 0)
        return 0;

    position_bytes = (u32)batch->vertex_count * 3u * sizeof(s16);
    color_bytes = (u32)batch->vertex_count * 4u * sizeof(u8);
    uv_lanes = batch->uv_v2_16 ? 2u : 4u;
    uv_bytes = (u32)batch->vertex_count * uv_lanes * sizeof(u16);

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
            batch->uvs_16,
            batch->uvs_qwords,
            uv_bytes))
        return 0;

    return 1;
}

static void CTRPS2_NativeGeometryBuildHeader(
    qword_t header[CTRPS2_NATIVE_GEOMETRY_HEADER_QWORDS],
    const struct CTRPS2NativeGeometryBatch *batch,
    int emit_finish)
{
    u64 prim;
    const float depth_scale =
        (CTRPS2_NATIVE_DEPTH_MAX * CTRPS2_NATIVE_DEPTH_NEAR_Z) / 16.0f;

    memset(header, 0, sizeof(qword_t) * CTRPS2_NATIVE_GEOMETRY_HEADER_QWORDS);

    CTRPS2_NativeGeometryWriteFloat(
        &header[0], 0, CTRPS2_FRAME_WIDTH * 0.5f);
    CTRPS2_NativeGeometryWriteFloat(
        &header[0], 1, CTRPS2_FRAME_HEIGHT * 0.5f);
    CTRPS2_NativeGeometryWriteFloat(&header[0], 2, depth_scale);
    header[0].sw[3] = batch->vertex_count;

    CTRPS2_NativeGeometryWriteFloat(
        &header[1], 0,
        (float)(CTRPS2_GS_ORIGIN_X + (CTRPS2_FRAME_WIDTH / 2)));
    CTRPS2_NativeGeometryWriteFloat(
        &header[1], 1,
        (float)(CTRPS2_GS_ORIGIN_Y + (CTRPS2_FRAME_HEIGHT / 2)));
    CTRPS2_NativeGeometryWriteFloat(&header[1], 2, 0.0f);

    /*
     * p2trk v2 owns the logical texture dimensions. The reciprocal is computed
     * once while the persistent pass packet is built, not per frame and never
     * per vertex. The asset compiler remains free to choose a wider GS TBW.
     */
    CTRPS2_NativeGeometryWriteFloat(
        &header[3], 0, 1.0f / (float)batch->texture_width);
    CTRPS2_NativeGeometryWriteFloat(
        &header[3], 1, 1.0f / (float)batch->texture_height);
    CTRPS2_NativeGeometryWriteFloat(&header[3], 2, 1.0f);

    header[4].sw[3] = emit_finish ? 1u : 0u;

    prim = GS_SET_PRIM(
        batch->gs_primitive,
        PRIM_SHADE_GOURAUD,
        DRAW_ENABLE,
        DRAW_DISABLE,
        DRAW_DISABLE,
        DRAW_DISABLE,
        PRIM_MAP_ST,
        0,
        PRIM_UNFIXED);

    header[2].dw[0] = VU_GS_GIFTAG(
        batch->vertex_count,
        emit_finish ? 0 : 1,
        1,
        prim,
        GIF_FLG_PACKED,
        CTRPS2_NATIVE_GEOMETRY_PACKED_NREG);
    header[2].dw[1] = CTRPS2_NATIVE_GEOMETRY_PACKED_REGLIST;

    header[5].dw[0] = GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1);
    header[5].dw[1] = GIF_REG_AD;
    header[6].dw[0] = 1;
    header[6].dw[1] = GS_REG_FINISH;
}

static void CTRPS2_NativeGeometryAddInlineHeader(
    packet2_t *packet,
    const struct CTRPS2NativeGeometryBatch *batch,
    int emit_finish)
{
    qword_t header[CTRPS2_NATIVE_GEOMETRY_HEADER_QWORDS]
        __attribute__((aligned(16)));

    CTRPS2_NativeGeometryBuildHeader(header, batch, emit_finish);

    packet2_utils_vu_open_unpack(packet, 0, 1);
    packet2_add_data(packet, header, CTRPS2_NATIVE_GEOMETRY_HEADER_QWORDS);
    packet2_utils_vu_close_unpack(packet);
}

static void CTRPS2_NativeGeometryAddPositionUnpack(
    packet2_t *packet,
    const struct CTRPS2NativeGeometryBatch *batch)
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
        (void *)batch->positions_v3_16,
        batch->positions_qwords,
        0, 0, 0);
    packet2_vif_stcycl(packet, 0, 0x0101, 0);
    packet2_vif_open_unpack(
        packet,
        P2_UNPACK_V3_16,
        CTRPS2_NATIVE_GEOMETRY_POSITION_DEST_QW,
        1, 1, 0, 0);
    packet2_vif_close_unpack_manual(packet, batch->vertex_count);
}

static void CTRPS2_NativeGeometryAddColorUnpack(
    packet2_t *packet,
    const struct CTRPS2NativeGeometryBatch *batch)
{
    packet2_chain_ref(
        packet,
        (void *)batch->colors_rgba8,
        batch->colors_qwords,
        0, 0, 0);
    packet2_vif_stcycl(packet, 0, 0x0101, 0);
    packet2_vif_open_unpack(
        packet,
        P2_UNPACK_V4_8,
        CTRPS2_NATIVE_GEOMETRY_POSITION_DEST_QW + batch->vertex_count,
        1, 0, 1, 0);
    packet2_vif_close_unpack_manual(packet, batch->vertex_count);
}

static void CTRPS2_NativeGeometryAddUVUnpack(
    packet2_t *packet,
    const struct CTRPS2NativeGeometryBatch *batch)
{
    enum UnpackMode unpack_mode =
        batch->uv_v2_16 ? P2_UNPACK_V2_16 : P2_UNPACK_V4_16;

    packet2_chain_ref(
        packet,
        (void *)batch->uvs_16,
        batch->uvs_qwords,
        0, 0, 0);
    packet2_vif_stcycl(packet, 0, 0x0101, 0);
    packet2_vif_open_unpack(
        packet,
        unpack_mode,
        CTRPS2_NATIVE_GEOMETRY_POSITION_DEST_QW +
            ((u32)batch->vertex_count * 2u),
        1, 0, 1, 0);
    packet2_vif_close_unpack_manual(packet, batch->vertex_count);
}

static void CTRPS2_NativeGeometryAddLaunch(packet2_t *packet, u32 batch_index)
{
    int need_path_flush;

    /*
     * Default N1c: FLUSH before every MSCAL.
     * N1d A/B: MSCAL-only while the alternate output is disjoint, FLUSH when
     * the same TOP/TOPS output region is reused two batches later.
     */
    need_path_flush = !CTRPS2_NATIVE_VIF_REUSE_AWARE ||
                      (batch_index >= 2u && ((batch_index & 1u) == 0u));

    packet2_chain_open_cnt(packet, 0, 0, 0);
    if (need_path_flush)
        packet2_vif_flush(packet, 0);
    packet2_vif_mscal(packet, CTRPS2_NATIVE_GEOMETRY_PROGRAM_ADDR, 0);
    packet2_chain_close_tag(packet);
}

int CTRPS2_NativeGeometryInit(void)
{
    if (!CTRPS2_NativeGeometryUploadProgram())
        return 0;
    if (!CTRPS2_NativeGeometryUploadMatrix())
        return 0;

    s_initialized = 1;
    s_passReady = 0;
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

int CTRPS2_NativeGeometryPassBegin(u32 batch_count)
{
    u32 packet_qwords;

    if (!s_initialized || s_submitted || batch_count == 0)
        return 0;

    if (batch_count >
        ((0xffffu - CTRPS2_NATIVE_GEOMETRY_PACKET_BASE_QWORDS) /
         CTRPS2_NATIVE_GEOMETRY_PACKET_QWORDS_PER_BATCH))
        return 0;

    packet_qwords = CTRPS2_NATIVE_GEOMETRY_PACKET_BASE_QWORDS +
                    (batch_count * CTRPS2_NATIVE_GEOMETRY_PACKET_QWORDS_PER_BATCH);

    if (s_vifPacket != NULL)
    {
        packet2_free(s_vifPacket);
        s_vifPacket = NULL;
    }

    s_vifPacket = packet2_create(
        (u16)packet_qwords,
        P2_TYPE_NORMAL,
        P2_MODE_CHAIN,
        1);
    if (s_vifPacket == NULL)
        return 0;

    s_expectedBatches = batch_count;
    s_appendedBatches = 0;
    s_passReady = 0;
    return 1;
}

int CTRPS2_NativeGeometryPassAppend(
    const struct CTRPS2NativeGeometryBatch *batch)
{
    int emit_finish;

    if (s_vifPacket == NULL || s_passReady || s_submitted)
        return 0;
    if (s_appendedBatches >= s_expectedBatches)
        return 0;
    if (!CTRPS2_NativeGeometryBatchValid(batch))
        return 0;

    emit_finish = ((s_appendedBatches + 1u) == s_expectedBatches);

    CTRPS2_NativeGeometryAddInlineHeader(s_vifPacket, batch, emit_finish);
    CTRPS2_NativeGeometryAddPositionUnpack(s_vifPacket, batch);
    CTRPS2_NativeGeometryAddColorUnpack(s_vifPacket, batch);
    CTRPS2_NativeGeometryAddUVUnpack(s_vifPacket, batch);
    CTRPS2_NativeGeometryAddLaunch(s_vifPacket, s_appendedBatches);

    s_appendedBatches++;
    return 1;
}

int CTRPS2_NativeGeometryPassEnd(void)
{
    if (s_vifPacket == NULL || s_submitted)
        return 0;
    if (s_appendedBatches != s_expectedBatches)
        return 0;

    packet2_utils_vu_add_end_tag(s_vifPacket);
    s_passReady = 1;
    return 1;
}

int CTRPS2_NativeGeometryPassSubmit(void)
{
    if (!s_initialized || !s_passReady || s_vifPacket == NULL || s_submitted)
        return 0;

    dma_channel_send_packet2(s_vifPacket, DMA_CHANNEL_VIF1, 1);
    s_submitted = 1;
    return 1;
}

void CTRPS2_NativeGeometryPassWait(void)
{
    if (!s_submitted)
        return;

    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    draw_wait_finish();
    s_submitted = 0;
}
