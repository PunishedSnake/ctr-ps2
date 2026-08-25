#include "renderer_ps2.h"

#include <dma.h>
#include <draw.h>
#include <draw3d.h>
#include <gif_tags.h>
#include <graph.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>
#include <packet2.h>
#include <packet2_utils.h>
#include <packet2_vif.h>
#include <tamtypes.h>

#define CTRPS2_FRAME_WIDTH        640
#define CTRPS2_FRAME_HEIGHT       448
#define CTRPS2_GS_ORIGIN_X        (2048 - (CTRPS2_FRAME_WIDTH / 2))
#define CTRPS2_GS_ORIGIN_Y        (2048 - (CTRPS2_FRAME_HEIGHT / 2))

/*
 * VU1 has 1024 qwords of data memory. The first renderer contract reserves
 * two equal VIF1 TOP/TOPS regions. This is deliberately larger than the tiny
 * bootstrap packet so the same ownership model can survive the next step,
 * where each region holds a real CTR geometry batch plus VU-generated output.
 */
#define CTRPS2_VU1_DB_BASE        0
#define CTRPS2_VU1_DB_OFFSET      512
#define CTRPS2_VIF_PACKET_QWORDS  32
#define CTRPS2_GIF_PACKET_QWORDS  16

extern u32 CTRPS2_VU1_BootStart __attribute__((section(".vudata")));
extern u32 CTRPS2_VU1_BootEnd __attribute__((section(".vudata")));

static framebuffer_t s_frame;
static zbuffer_t s_zbuffer;
static packet2_t *s_bootGifPacket;
static packet2_t *s_bootVifPacket;
static int s_bootSubmitted;

static xyz_t CTRPS2_MakeXYZ(int x, int y, u32 z)
{
    xyz_t result;

    result.x = (u16)((CTRPS2_GS_ORIGIN_X + x) << 4);
    result.y = (u16)((CTRPS2_GS_ORIGIN_Y + y) << 4);
    result.z = z;
    return result;
}

static color_t CTRPS2_MakeColor(u8 r, u8 g, u8 b)
{
    color_t result;

    result.r = r;
    result.g = g;
    result.b = b;
    result.a = 0x80;
    result.q = 1.0f;
    return result;
}

static int CTRPS2_InitGs(void)
{
    packet2_t *setup;
    qword_t *q;

    graph_vram_clear();

    s_frame.width = CTRPS2_FRAME_WIDTH;
    s_frame.height = CTRPS2_FRAME_HEIGHT;
    s_frame.mask = 0;
    s_frame.psm = GS_PSM_16S;
    s_frame.address = graph_vram_allocate(
        s_frame.width,
        s_frame.height,
        s_frame.psm,
        GRAPH_ALIGN_PAGE);

    s_zbuffer.enable = DRAW_ENABLE;
    s_zbuffer.method = ZTEST_METHOD_GREATER_EQUAL;
    s_zbuffer.mask = 0;
    s_zbuffer.zsm = GS_ZBUF_16S;
    s_zbuffer.address = graph_vram_allocate(
        s_frame.width,
        s_frame.height,
        s_zbuffer.zsm,
        GRAPH_ALIGN_PAGE);

    if ((s_frame.address < 0) || (s_zbuffer.address < 0))
        return 0;

    if (graph_initialize(
            s_frame.address,
            s_frame.width,
            s_frame.height,
            s_frame.psm,
            0,
            0) < 0)
        return 0;

    setup = packet2_create(32, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    if (setup == NULL)
        return 0;

    q = setup->next;
    q = draw_setup_environment(q, 0, &s_frame, &s_zbuffer);
    q = draw_primitive_xyoffset(q, 0, CTRPS2_GS_ORIGIN_X, CTRPS2_GS_ORIGIN_Y);
    q = draw_disable_tests(q, 0, &s_zbuffer);
    q = draw_clear(
        q,
        0,
        CTRPS2_GS_ORIGIN_X,
        CTRPS2_GS_ORIGIN_Y,
        CTRPS2_FRAME_WIDTH,
        CTRPS2_FRAME_HEIGHT,
        12,
        18,
        30);
    q = draw_enable_tests(q, 0, &s_zbuffer);
    q = draw_finish(q);
    packet2_update(setup, q);

    dma_channel_send_packet2(setup, DMA_CHANNEL_GIF, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    draw_wait_finish();
    packet2_free(setup);
    return 1;
}

static int CTRPS2_UploadVu1Program(void)
{
    packet2_t *upload;
    u32 qwords;

    qwords = packet2_utils_get_packet_size_for_program(
        &CTRPS2_VU1_BootStart,
        &CTRPS2_VU1_BootEnd) + 2;

    upload = packet2_create((u16)qwords, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    if (upload == NULL)
        return 0;

    packet2_vif_add_micro_program(
        upload,
        0,
        &CTRPS2_VU1_BootStart,
        &CTRPS2_VU1_BootEnd);
    packet2_utils_vu_add_end_tag(upload);

    dma_channel_send_packet2(upload, DMA_CHANNEL_VIF1, 1);
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    packet2_free(upload);
    return 1;
}

static int CTRPS2_BuildBootstrapGifPacket(void)
{
    prim_t prim;
    color_t colors[3];
    xyz_t vertices[3];
    int i;

    s_bootGifPacket = packet2_create(
        CTRPS2_GIF_PACKET_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_NORMAL,
        0);
    if (s_bootGifPacket == NULL)
        return 0;

    prim.type = PRIM_TRIANGLE;
    prim.shading = PRIM_SHADE_GOURAUD;
    prim.mapping = DRAW_DISABLE;
    prim.fogging = DRAW_DISABLE;
    prim.blending = DRAW_DISABLE;
    prim.antialiasing = DRAW_DISABLE;
    prim.mapping_type = PRIM_MAP_ST;
    prim.colorfix = PRIM_UNFIXED;

    colors[0] = CTRPS2_MakeColor(0x80, 0x18, 0x18);
    colors[1] = CTRPS2_MakeColor(0x18, 0x80, 0x38);
    colors[2] = CTRPS2_MakeColor(0x28, 0x48, 0x80);

    vertices[0] = CTRPS2_MakeXYZ(320, 72, 0x10000000u);
    vertices[1] = CTRPS2_MakeXYZ(128, 360, 0x10000000u);
    vertices[2] = CTRPS2_MakeXYZ(512, 360, 0x10000000u);

    /* State packet, EOP=0. */
    packet2_add_2x_s64(
        s_bootGifPacket,
        (s64)GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1),
        (s64)GIF_REG_AD);
    packet2_add_2x_s64(
        s_bootGifPacket,
        (s64)GS_SET_PRIM(
            prim.type,
            prim.shading,
            prim.mapping,
            prim.fogging,
            prim.blending,
            prim.antialiasing,
            prim.mapping_type,
            0,
            prim.colorfix),
        (s64)GS_REG_PRIM);

    /* Geometry packet, EOP=0 so FINISH can remain in the same XGKICK stream. */
    packet2_add_2x_s64(
        s_bootGifPacket,
        (s64)VU_GS_GIFTAG(3, 0, 0, 0, GIF_FLG_REGLIST, 2),
        (s64)DRAW_RGBAQ_REGLIST);

    for (i = 0; i < 3; ++i)
    {
        qword_t *vertex = s_bootGifPacket->next;
        vertex->dw[0] = colors[i].rgbaq;
        vertex->dw[1] = vertices[i].xyz;
        packet2_update(s_bootGifPacket, vertex + 1);
    }

    /* FINISH packet owns EOP=1. CPU can therefore wait on a real GS fence. */
    packet2_add_2x_s64(
        s_bootGifPacket,
        (s64)GIF_SET_TAG(1, 1, 0, 0, GIF_FLG_PACKED, 1),
        (s64)GIF_REG_AD);
    packet2_add_2x_s64(s_bootGifPacket, 1, (s64)GS_REG_FINISH);

    return 1;
}

static int CTRPS2_BuildBootstrapVifPacket(void)
{
    u32 gifQwords = packet2_get_qw_count(s_bootGifPacket);

    /*
     * TTE=1 is part of this packet contract. packet2's VIF chain helpers place
     * their two VIF codes in the DMA tag transfer payload. With TTE disabled,
     * those words would instead be interpreted as the next DMA tag.
     */
    s_bootVifPacket = packet2_create(
        CTRPS2_VIF_PACKET_QWORDS,
        P2_TYPE_NORMAL,
        P2_MODE_CHAIN,
        1);
    if (s_bootVifPacket == NULL)
        return 0;

    packet2_utils_vu_add_double_buffer(
        s_bootVifPacket,
        CTRPS2_VU1_DB_BASE,
        CTRPS2_VU1_DB_OFFSET);

    packet2_utils_vu_add_unpack_data(
        s_bootVifPacket,
        0,
        s_bootGifPacket->base,
        gifQwords,
        1);

    /*
     * CURRENT IMPLEMENTATION baseline: PS2SDK's helper emits FLUSH + MSCAL.
     * The broad FLUSH is intentionally retained for the first hardware proof.
     * It is not treated as a permanent renderer barrier and will be removed or
     * narrowed only after VIF/VU/GIF ownership is validated on real hardware.
     */
    packet2_utils_vu_add_start_program(s_bootVifPacket, 0);
    packet2_utils_vu_add_end_tag(s_bootVifPacket);
    return 1;
}

int CTRPS2_RendererInit(void)
{
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_initialize(DMA_CHANNEL_VIF1, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    dma_channel_fast_waits(DMA_CHANNEL_VIF1);

    if (!CTRPS2_InitGs())
        return 0;
    if (!CTRPS2_UploadVu1Program())
        return 0;
    if (!CTRPS2_BuildBootstrapGifPacket())
        return 0;
    if (!CTRPS2_BuildBootstrapVifPacket())
        return 0;

    s_bootSubmitted = 0;
    return 1;
}

void CTRPS2_RendererSubmitBootstrap(void)
{
    if (s_bootSubmitted)
        return;

    /*
     * Source-chain REF points at the persistent GIF payload. flush_cache=1 is
     * required by current PS2SDK because source-chained data is not covered by
     * the normal chain cache sync alone.
     */
    dma_channel_send_packet2(s_bootVifPacket, DMA_CHANNEL_VIF1, 1);
    s_bootSubmitted = 1;
}

void CTRPS2_RendererWaitForBootstrap(void)
{
    if (!s_bootSubmitted)
        return;

    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    draw_wait_finish();
}

void CTRPS2_RendererPresent(void)
{
    graph_wait_vsync();
}
