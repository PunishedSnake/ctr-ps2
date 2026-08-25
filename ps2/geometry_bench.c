#include "geometry_bench.h"
#include "renderer_ps2.h"

#include <dma.h>
#include <draw.h>
#include <draw3d.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <packet2.h>
#include <packet2_chain.h>
#include <packet2_utils.h>
#include <packet2_vif.h>
#include <string.h>
#include <tamtypes.h>

#define CTRPS2_GEOMETRY_PROGRAM_ADDR      64
#define CTRPS2_GEOMETRY_VERTEX_COUNT      3
#define CTRPS2_GEOMETRY_HEADER_QWORDS     7
#define CTRPS2_GEOMETRY_POSITION_QWORDS   2
#define CTRPS2_GEOMETRY_VIF_QWORDS        48
#define CTRPS2_GEOMETRY_POSITION_DEST_QW  7

extern u32 CTRPS2_VU1_GeometryStart __attribute__((section(".vudata")));
extern u32 CTRPS2_VU1_GeometryEnd __attribute__((section(".vudata")));

/*
 * Producer: offline-asset-shaped test data in RDRAM.
 * Consumer: VIF1 -> VU1.
 * Representation: signed V3-16, 12.4 fixed point, 6 bytes/vertex.
 * Alignment: cache-line aligned source object; VIF consumes only the first 18
 * bytes and the DMA REF carries two qwords so the final 14 bytes are padding.
 */
static qword_t s_packedPositions[CTRPS2_GEOMETRY_POSITION_QWORDS]
    __attribute__((aligned(64)));

/* Header is persistent and copied by VIF into the active TOPS buffer. */
static qword_t s_geometryHeader[CTRPS2_GEOMETRY_HEADER_QWORDS]
    __attribute__((aligned(64)));

/* Matrix columns live outside TOP/TOPS so both VU1 buffers can share them. */
static const float s_objectToScreen[16] __attribute__((aligned(64))) = {
     1.0f,  0.0f, 0.0f, 0.0f,
     0.0f, -1.0f, 0.0f, 0.0f,
     0.0f,  0.0f, 1.0f, 0.0f,
     0.0f,  0.0f, 0.0f, 1.0f,
};

static packet2_t *s_geometryVifPacket;
static int s_geometrySubmitted;

static void CTRPS2_WriteFloat(qword_t *qword, int component, float value)
{
    union
    {
        float f;
        u32 u;
    } bits;

    bits.f = value;
    qword->sw[component] = bits.u;
}

static int CTRPS2_UploadGeometryProgram(void)
{
    packet2_t *upload;
    u32 qwords;

    qwords = packet2_utils_get_packet_size_for_program(
        &CTRPS2_VU1_GeometryStart,
        &CTRPS2_VU1_GeometryEnd) + 2;

    upload = packet2_create((u16)qwords, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    if (upload == NULL)
        return 0;

    packet2_vif_add_micro_program(
        upload,
        CTRPS2_GEOMETRY_PROGRAM_ADDR,
        &CTRPS2_VU1_GeometryStart,
        &CTRPS2_VU1_GeometryEnd);
    packet2_utils_vu_add_end_tag(upload);

    dma_channel_send_packet2(upload, DMA_CHANNEL_VIF1, 1);
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    packet2_free(upload);
    return 1;
}

static int CTRPS2_UploadGeometryMatrix(void)
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

static void CTRPS2_BuildPackedPositions(void)
{
    s16 *dst = (s16 *)s_packedPositions;

    memset(s_packedPositions, 0, sizeof(s_packedPositions));

    /* Signed 12.4 fixed point: [-1, +1] maps to roughly [-16, +16]. */
    dst[0] = -8;
    dst[1] = -8;
    dst[2] = 0;

    dst[3] = 0;
    dst[4] = 10;
    dst[5] = 0;

    dst[6] = 8;
    dst[7] = -8;
    dst[8] = 0;
}

static void CTRPS2_BuildGeometryHeader(void)
{
    u64 prim;
    float zScale = ((float)0x00ffffffu) / 32.0f;

    memset(s_geometryHeader, 0, sizeof(s_geometryHeader));

    /* scale.xyz + count.w */
    CTRPS2_WriteFloat(&s_geometryHeader[0], 0, CTRPS2_FRAME_WIDTH * 0.5f);
    CTRPS2_WriteFloat(&s_geometryHeader[0], 1, CTRPS2_FRAME_HEIGHT * 0.5f);
    CTRPS2_WriteFloat(&s_geometryHeader[0], 2, zScale);
    s_geometryHeader[0].sw[3] = CTRPS2_GEOMETRY_VERTEX_COUNT;

    /* Distinct screen offset lets scale and GS XYOFFSET remain independent. */
    CTRPS2_WriteFloat(
        &s_geometryHeader[1],
        0,
        (float)(CTRPS2_GS_ORIGIN_X + (CTRPS2_FRAME_WIDTH / 2)));
    CTRPS2_WriteFloat(
        &s_geometryHeader[1],
        1,
        (float)(CTRPS2_GS_ORIGIN_Y + (CTRPS2_FRAME_HEIGHT / 2)));
    CTRPS2_WriteFloat(&s_geometryHeader[1], 2, zScale);

    prim = GS_SET_PRIM(
        PRIM_TRIANGLE,
        PRIM_SHADE_GOURAUD,
        DRAW_DISABLE,
        DRAW_DISABLE,
        DRAW_DISABLE,
        DRAW_DISABLE,
        PRIM_MAP_ST,
        0,
        PRIM_UNFIXED);

    /* VU copies this GS-ready primitive tag to its output region. */
    s_geometryHeader[2].dw[0] = VU_GS_GIFTAG(
        CTRPS2_GEOMETRY_VERTEX_COUNT,
        0,
        1,
        prim,
        GIF_FLG_REGLIST,
        3);
    s_geometryHeader[2].dw[1] = DRAW_STQ2_REGLIST;

    /* REGLIST-friendly RGBA representation used by the VU1 path. */
    s_geometryHeader[3].sw[0] = 0x28;
    s_geometryHeader[3].sw[1] = 0x70;
    s_geometryHeader[3].sw[2] = 0x80;
    s_geometryHeader[3].sw[3] = 0x80;

    /* STQ base. Texture mapping is disabled, but Q remains well-defined. */
    CTRPS2_WriteFloat(&s_geometryHeader[4], 3, 1.0f);

    /* Final GS correctness fence is part of the same XGKICK stream. */
    s_geometryHeader[5].dw[0] = GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1);
    s_geometryHeader[5].dw[1] = GIF_REG_AD;
    s_geometryHeader[6].dw[0] = 1;
    s_geometryHeader[6].dw[1] = GS_REG_FINISH;
}

static void CTRPS2_AddV3_16PositionUnpack(packet2_t *packet)
{
    static const u32 row[4] = {0, 0, 0, 0x3f800000u};
    Mask mask;

    mask.m = 0;
    mask.m3 = 1;
    mask.m7 = 1;
    mask.m11 = 1;
    mask.m15 = 1;

    /* ROW.W supplies homogeneous W=1.0 without source bandwidth. */
    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_strow(packet, row, 0);
    packet2_vif_nop(packet, 0);
    packet2_chain_close_tag(packet);

    /* X/Y/Z come from input; W comes from ROW for every write-cycle group. */
    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_stmask(packet, mask, 0);
    packet2_chain_close_tag(packet);

    /*
     * REF source is two qwords (32 bytes) while UNPACK consumes only 18 bytes
     * for three V3-16 vertices. The remainder is explicit packet padding.
     */
    packet2_chain_ref(
        packet,
        s_packedPositions,
        CTRPS2_GEOMETRY_POSITION_QWORDS,
        0,
        0,
        0);
    packet2_vif_stcycl(packet, 0, 0x0101, 0);
    packet2_vif_open_unpack(
        packet,
        P2_UNPACK_V3_16,
        CTRPS2_GEOMETRY_POSITION_DEST_QW,
        1,
        1,
        0,
        0);
    packet2_vif_close_unpack_manual(packet, CTRPS2_GEOMETRY_VERTEX_COUNT);
}

static int CTRPS2_BuildGeometryVifPacket(void)
{
    s_geometryVifPacket = packet2_create(
        CTRPS2_GEOMETRY_VIF_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_CHAIN,
        1);
    if (s_geometryVifPacket == NULL)
        return 0;

    packet2_utils_vu_add_unpack_data(
        s_geometryVifPacket,
        0,
        s_geometryHeader,
        CTRPS2_GEOMETRY_HEADER_QWORDS,
        1);

    CTRPS2_AddV3_16PositionUnpack(s_geometryVifPacket);

    /*
     * CURRENT IMPLEMENTATION baseline. This helper emits FLUSH + MSCAL.
     * The packed format and ownership model are the experiment here; barrier
     * narrowing belongs to the next A/B once this exact stream is reproduced.
     */
    packet2_utils_vu_add_start_program(
        s_geometryVifPacket,
        CTRPS2_GEOMETRY_PROGRAM_ADDR);
    packet2_utils_vu_add_end_tag(s_geometryVifPacket);
    return 1;
}

int CTRPS2_GeometryBenchInit(void)
{
    CTRPS2_BuildPackedPositions();
    CTRPS2_BuildGeometryHeader();

    if (!CTRPS2_UploadGeometryProgram())
        return 0;
    if (!CTRPS2_UploadGeometryMatrix())
        return 0;
    if (!CTRPS2_BuildGeometryVifPacket())
        return 0;

    s_geometrySubmitted = 0;
    return 1;
}

void CTRPS2_GeometryBenchSubmit(void)
{
    if (s_geometrySubmitted)
        return;

    /* Persistent REF sources require coherency before source-chain DMA. */
    dma_channel_send_packet2(s_geometryVifPacket, DMA_CHANNEL_VIF1, 1);
    s_geometrySubmitted = 1;
}

void CTRPS2_GeometryBenchWait(void)
{
    if (!s_geometrySubmitted)
        return;

    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    draw_wait_finish();
}
