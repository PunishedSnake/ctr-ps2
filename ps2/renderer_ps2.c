#include "renderer_ps2.h"

#include <dma.h>
#include <draw.h>
#include <graph.h>
#include <gs_psm.h>
#include <packet2.h>
#include <packet2_utils.h>
#include <tamtypes.h>

static framebuffer_t s_frame;
static zbuffer_t s_zbuffer;

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

    /*
     * M2 graphics prototype: keep Z writes available but accept every fragment.
     * The fixture is coplanar and depth ordering proves nothing yet; removing Z
     * rejection also removes dependence on uninitialized/uncleared Z contents.
     * Real track rendering will restore the measured game depth contract.
     */
    s_zbuffer.enable = DRAW_ENABLE;
    s_zbuffer.method = ZTEST_METHOD_ALLPASS;
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

static int CTRPS2_ConfigureVu1Buffers(void)
{
    packet2_t *packet;

    packet = packet2_create(4, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    if (packet == NULL)
        return 0;

    packet2_utils_vu_add_double_buffer(
        packet,
        CTRPS2_VU1_DB_BASE,
        CTRPS2_VU1_DB_OFFSET);
    packet2_utils_vu_add_end_tag(packet);

    dma_channel_send_packet2(packet, DMA_CHANNEL_VIF1, 1);
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    packet2_free(packet);
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
    if (!CTRPS2_ConfigureVu1Buffers())
        return 0;

    return 1;
}

void CTRPS2_RendererClear(u8 r, u8 g, u8 b)
{
    packet2_t *clear;
    qword_t *q;

    clear = packet2_create(32, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    if (clear == NULL)
        return;

    q = clear->next;
    q = draw_disable_tests(q, 0, &s_zbuffer);
    q = draw_clear(
        q,
        0,
        CTRPS2_GS_ORIGIN_X,
        CTRPS2_GS_ORIGIN_Y,
        CTRPS2_FRAME_WIDTH,
        CTRPS2_FRAME_HEIGHT,
        r,
        g,
        b);
    q = draw_enable_tests(q, 0, &s_zbuffer);
    q = draw_finish(q);
    packet2_update(clear, q);

    dma_channel_send_packet2(clear, DMA_CHANNEL_GIF, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    draw_wait_finish();
    packet2_free(clear);
}

void CTRPS2_RendererPresent(void)
{
    graph_wait_vsync();
}
