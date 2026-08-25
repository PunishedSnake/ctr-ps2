#include "geometry_bench.h"
#include "renderer_ps2.h"

#include <dma.h>
#include <draw.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <packet2.h>
#include <packet2_chain.h>
#include <packet2_utils.h>
#include <packet2_vif.h>
#include <string.h>
#include <tamtypes.h>

#define CTRPS2_GEOMETRY_PROGRAM_ADDR       64
#define CTRPS2_GEOMETRY_HEADER_QWORDS      7
#define CTRPS2_GEOMETRY_VIF_QWORDS         96
#define CTRPS2_GEOMETRY_POSITION_DEST_QW   7
#define CTRPS2_GEOMETRY_OUTPUT_DEST_QW     96
#define CTRPS2_GEOMETRY_PACKED_NREG        3
#define CTRPS2_GEOMETRY_PACKED_REGLIST \
    (((u64)GIF_REG_RGBAQ) | ((u64)GIF_REG_UV << 4) | ((u64)GIF_REG_XYZ2 << 8))

/*
 * M3 input expands to three VU qwords per vertex: position, color and UV.
 * Output starts at TOP+96, so floor((96-7)/3) = 29 vertices remain legal.
 * The current QuadBlock strip uses 22. Output itself is 1 + 3*N + 2 qwords,
 * well inside one 496-qword TOP/TOPS buffer.
 */
#define CTRPS2_GEOMETRY_MAX_VERTICES \
    ((CTRPS2_GEOMETRY_OUTPUT_DEST_QW - CTRPS2_GEOMETRY_POSITION_DEST_QW) / 3u)
#define CTRPS2_GEOMETRY_MAX_POSITION_QWORDS \
    ((CTRPS2_GEOMETRY_MAX_VERTICES * 3u * sizeof(s16) + 15u) / 16u)
#define CTRPS2_GEOMETRY_MAX_COLOR_QWORDS \
    ((CTRPS2_GEOMETRY_MAX_VERTICES * 4u * sizeof(u8) + 15u) / 16u)
#define CTRPS2_GEOMETRY_MAX_UV_QWORDS \
    ((CTRPS2_GEOMETRY_MAX_VERTICES * 4u * sizeof(u16) + 15u) / 16u)

extern u32 CTRPS2_VU1_GeometryStart __attribute__((section(".vudata")));
extern u32 CTRPS2_VU1_GeometryEnd __attribute__((section(".vudata")));

/*
 * Producer: benchmark/bridge input in RDRAM.
 * Consumer: VIF1 -> VU1 -> XGKICK -> GS.
 * Position representation: signed V3-16, 6 bytes/vertex.
 * Color representation: unsigned RGBA8, 4 bytes/vertex.
 * UV representation: unsigned V4-16, 8 bytes/vertex; U/V are GS 12.4 fixed.
 *
 * VIF1 expands each stream to one qword per vertex. The VU program converts
 * only positions to float, leaving color and UV lanes in GS-ready integer form.
 */
static qword_t s_packedPositions[CTRPS2_GEOMETRY_MAX_POSITION_QWORDS]
    __attribute__((aligned(64)));
static qword_t s_packedColors[CTRPS2_GEOMETRY_MAX_COLOR_QWORDS]
    __attribute__((aligned(64)));
static qword_t s_packedUVs[CTRPS2_GEOMETRY_MAX_UV_QWORDS]
    __attribute__((aligned(64)));

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
static u32 s_geometryVertexCount;
static u32 s_geometryPositionQwords;
static u32 s_geometryColorQwords;
static u32 s_geometryUVQwords;
static int s_geometryTextured;
static int s_geometrySubmitted;
static int s_geometryInitialized;

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

static void CTRPS2_BuildGeometryHeader(int primitive)
{
    u64 prim;
    float zScale = ((float)0x00ffffffu) / 32.0f;

    memset(s_geometryHeader, 0, sizeof(s_geometryHeader));

    CTRPS2_WriteFloat(&s_geometryHeader[0], 0, CTRPS2_FRAME_WIDTH * 0.5f);
    CTRPS2_WriteFloat(&s_geometryHeader[0], 1, CTRPS2_FRAME_HEIGHT * 0.5f);
    CTRPS2_WriteFloat(&s_geometryHeader[0], 2, zScale);
    s_geometryHeader[0].sw[3] = s_geometryVertexCount;

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
        primitive,
        PRIM_SHADE_GOURAUD,
        s_geometryTextured ? DRAW_ENABLE : DRAW_DISABLE,
        DRAW_DISABLE,
        DRAW_DISABLE,
        DRAW_DISABLE,
        PRIM_MAP_UV,
        0,
        PRIM_UNFIXED);

    /* One GS PACKED register qword for RGBAQ, UV and XYZ2 per vertex. */
    s_geometryHeader[2].dw[0] = VU_GS_GIFTAG(
        s_geometryVertexCount,
        0,
        1,
        prim,
        GIF_FLG_PACKED,
        CTRPS2_GEOMETRY_PACKED_NREG);
    s_geometryHeader[2].dw[1] = CTRPS2_GEOMETRY_PACKED_REGLIST;

    /* Header slots 3/4 remain reserved to keep the established input ABI. */

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

    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_strow(packet, row, 0);
    packet2_vif_nop(packet, 0);
    packet2_chain_close_tag(packet);

    packet2_chain_open_cnt(packet, 0, 0, 0);
    packet2_vif_stmask(packet, mask, 0);
    packet2_chain_close_tag(packet);

    packet2_chain_ref(
        packet,
        s_packedPositions,
        s_geometryPositionQwords,
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
    packet2_vif_close_unpack_manual(packet, s_geometryVertexCount);
}

static void CTRPS2_AddRGBA8ColorUnpack(packet2_t *packet)
{
    packet2_chain_ref(
        packet,
        s_packedColors,
        s_geometryColorQwords,
        0,
        0,
        0);
    packet2_vif_stcycl(packet, 0, 0x0101, 0);
    packet2_vif_open_unpack(
        packet,
        P2_UNPACK_V4_8,
        CTRPS2_GEOMETRY_POSITION_DEST_QW + s_geometryVertexCount,
        1,
        0,
        1,
        0);
    packet2_vif_close_unpack_manual(packet, s_geometryVertexCount);
}

static void CTRPS2_AddUV16Unpack(packet2_t *packet)
{
    packet2_chain_ref(
        packet,
        s_packedUVs,
        s_geometryUVQwords,
        0,
        0,
        0);
    packet2_vif_stcycl(packet, 0, 0x0101, 0);
    packet2_vif_open_unpack(
        packet,
        P2_UNPACK_V4_16,
        CTRPS2_GEOMETRY_POSITION_DEST_QW + (s_geometryVertexCount * 2u),
        1,
        0,
        1,
        0);
    packet2_vif_close_unpack_manual(packet, s_geometryVertexCount);
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
    CTRPS2_AddRGBA8ColorUnpack(s_geometryVifPacket);
    CTRPS2_AddUV16Unpack(s_geometryVifPacket);

    /* CURRENT IMPLEMENTATION: helper emits the conservative FLUSH + MSCAL. */
    packet2_utils_vu_add_start_program(
        s_geometryVifPacket,
        CTRPS2_GEOMETRY_PROGRAM_ADDR);
    packet2_utils_vu_add_end_tag(s_geometryVifPacket);
    return 1;
}

static int CTRPS2_GeometryBenchConfigureInternal(
    const void *positions_v3_16,
    const void *colors_rgba8,
    const void *uvs_v4_16,
    u32 vertex_count,
    int gs_primitive)
{
    static const u8 fallbackColor[4] = {0x80, 0x80, 0x80, 0x80};
    u32 position_bytes;
    u32 color_bytes;
    u32 uv_bytes;
    u32 i;

    if (!s_geometryInitialized)
        return 0;
    if (s_geometrySubmitted)
        return 0;
    if (positions_v3_16 == NULL)
        return 0;
    if (vertex_count == 0 || vertex_count > CTRPS2_GEOMETRY_MAX_VERTICES)
        return 0;

    position_bytes = vertex_count * 3u * sizeof(s16);
    color_bytes = vertex_count * 4u * sizeof(u8);
    uv_bytes = vertex_count * 4u * sizeof(u16);
    s_geometryPositionQwords = (position_bytes + 15u) / 16u;
    s_geometryColorQwords = (color_bytes + 15u) / 16u;
    s_geometryUVQwords = (uv_bytes + 15u) / 16u;
    s_geometryVertexCount = vertex_count;
    s_geometryTextured = (uvs_v4_16 != NULL);

    memset(s_packedPositions, 0, sizeof(s_packedPositions));
    memset(s_packedColors, 0, sizeof(s_packedColors));
    memset(s_packedUVs, 0, sizeof(s_packedUVs));
    memcpy(s_packedPositions, positions_v3_16, position_bytes);

    if (colors_rgba8 != NULL)
    {
        memcpy(s_packedColors, colors_rgba8, color_bytes);
    }
    else
    {
        u8 *dst = (u8 *)s_packedColors;
        for (i = 0; i < vertex_count; ++i)
            memcpy(dst + i * 4u, fallbackColor, sizeof(fallbackColor));
    }

    if (uvs_v4_16 != NULL)
        memcpy(s_packedUVs, uvs_v4_16, uv_bytes);

    CTRPS2_BuildGeometryHeader(gs_primitive);

    if (s_geometryVifPacket != NULL)
    {
        packet2_free(s_geometryVifPacket);
        s_geometryVifPacket = NULL;
    }

    return CTRPS2_BuildGeometryVifPacket();
}

int CTRPS2_GeometryBenchConfigureV3_16(
    const void *positions_v3_16,
    u32 vertex_count,
    int gs_primitive)
{
    return CTRPS2_GeometryBenchConfigureInternal(
        positions_v3_16,
        NULL,
        NULL,
        vertex_count,
        gs_primitive);
}

int CTRPS2_GeometryBenchConfigureV3_16_RGBA8(
    const void *positions_v3_16,
    const void *colors_rgba8,
    u32 vertex_count,
    int gs_primitive)
{
    if (colors_rgba8 == NULL)
        return 0;

    return CTRPS2_GeometryBenchConfigureInternal(
        positions_v3_16,
        colors_rgba8,
        NULL,
        vertex_count,
        gs_primitive);
}

int CTRPS2_GeometryBenchConfigureV3_16_RGBA8_UV16(
    const void *positions_v3_16,
    const void *colors_rgba8,
    const void *uvs_v4_16,
    u32 vertex_count,
    int gs_primitive)
{
    if (colors_rgba8 == NULL || uvs_v4_16 == NULL)
        return 0;

    return CTRPS2_GeometryBenchConfigureInternal(
        positions_v3_16,
        colors_rgba8,
        uvs_v4_16,
        vertex_count,
        gs_primitive);
}

int CTRPS2_GeometryBenchInit(void)
{
    if (!CTRPS2_UploadGeometryProgram())
        return 0;
    if (!CTRPS2_UploadGeometryMatrix())
        return 0;

    s_geometrySubmitted = 0;
    s_geometryInitialized = 1;
    return 1;
}

void CTRPS2_GeometryBenchSubmit(void)
{
    if (!s_geometryInitialized || s_geometryVifPacket == NULL)
        return;
    if (s_geometrySubmitted)
        return;

    /*
     * CURRENT IMPLEMENTATION: packet2 chain flush=1 performs FlushCache(0),
     * covering the persistent position/color/UV REF payloads as well as tags.
     */
    dma_channel_send_packet2(s_geometryVifPacket, DMA_CHANNEL_VIF1, 1);
    s_geometrySubmitted = 1;
}

void CTRPS2_GeometryBenchWait(void)
{
    if (!s_geometrySubmitted)
        return;

    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    draw_wait_finish();
    s_geometrySubmitted = 0;
}
