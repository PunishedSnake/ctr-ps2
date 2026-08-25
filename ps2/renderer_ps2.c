#include "renderer_ps2.h"

#include <dma.h>
#include <draw.h>
#include <draw_buffers.h>
#include <draw_sampling.h>
#include <graph.h>
#include <gs_psm.h>
#include <packet2.h>
#include <packet2_utils.h>
#include <tamtypes.h>

#define CTRPS2_DEBUG_TEXTURE_WIDTH   64
#define CTRPS2_DEBUG_TEXTURE_HEIGHT  64
#define CTRPS2_DEBUG_TEXTURE_PIXELS  (CTRPS2_DEBUG_TEXTURE_WIDTH * CTRPS2_DEBUG_TEXTURE_HEIGHT)

static framebuffer_t s_frame;
static zbuffer_t s_zbuffer;
static texbuffer_t s_debugTexture;
static u32 s_debugTexturePixels[CTRPS2_DEBUG_TEXTURE_PIXELS]
    __attribute__((aligned(64)));

static u32 CTRPS2_PackRGBA32(u8 r, u8 g, u8 b, u8 a)
{
    return ((u32)r) |
           ((u32)g << 8) |
           ((u32)b << 16) |
           ((u32)a << 24);
}

static void CTRPS2_BuildDebugTexture(void)
{
    int x;
    int y;

    /*
     * M3a orientation fixture. Four asymmetric quadrants, checker modulation,
     * white outer border and two differently colored diagonals make rotations,
     * mirroring and face-to-face UV discontinuities obvious on real hardware.
     * This is generated once at startup; it is not a shipping asset path.
     */
    for (y = 0; y < CTRPS2_DEBUG_TEXTURE_HEIGHT; ++y)
    {
        for (x = 0; x < CTRPS2_DEBUG_TEXTURE_WIDTH; ++x)
        {
            u8 r;
            u8 g;
            u8 b;
            int checker = ((x >> 3) ^ (y >> 3)) & 1;

            if (y < (CTRPS2_DEBUG_TEXTURE_HEIGHT / 2))
            {
                if (x < (CTRPS2_DEBUG_TEXTURE_WIDTH / 2))
                {
                    r = 0xd0;
                    g = 0x28;
                    b = 0x38;
                }
                else
                {
                    r = 0x28;
                    g = 0xc8;
                    b = 0x48;
                }
            }
            else
            {
                if (x < (CTRPS2_DEBUG_TEXTURE_WIDTH / 2))
                {
                    r = 0x28;
                    g = 0x48;
                    b = 0xd8;
                }
                else
                {
                    r = 0xd8;
                    g = 0xb8;
                    b = 0x28;
                }
            }

            if (checker)
            {
                r = (u8)((r * 3u) >> 2);
                g = (u8)((g * 3u) >> 2);
                b = (u8)((b * 3u) >> 2);
            }

            if (x == y)
            {
                r = 0xff;
                g = 0xff;
                b = 0xff;
            }
            else if ((x + y) == (CTRPS2_DEBUG_TEXTURE_WIDTH - 1))
            {
                r = 0xff;
                g = 0x50;
                b = 0xff;
            }

            if (x < 2 || y < 2 ||
                x >= (CTRPS2_DEBUG_TEXTURE_WIDTH - 2) ||
                y >= (CTRPS2_DEBUG_TEXTURE_HEIGHT - 2))
            {
                r = 0xf0;
                g = 0xf0;
                b = 0xf0;
            }

            s_debugTexturePixels[y * CTRPS2_DEBUG_TEXTURE_WIDTH + x] =
                CTRPS2_PackRGBA32(r, g, b, 0x80);
        }
    }
}

static int CTRPS2_UploadDebugTexture(void)
{
    packet2_t *packet;
    qword_t *q;

    packet = packet2_create(32, P2_TYPE_NORMAL, P2_MODE_CHAIN, 0);
    if (packet == NULL)
        return 0;

    q = packet->next;
    q = draw_texture_transfer(
        q,
        s_debugTexturePixels,
        CTRPS2_DEBUG_TEXTURE_WIDTH,
        CTRPS2_DEBUG_TEXTURE_HEIGHT,
        GS_PSM_32,
        s_debugTexture.address,
        s_debugTexture.width);
    q = draw_texture_flush(q);
    packet2_update(packet, q);

    /* draw_texture_transfer uses REF tags for the pixel payload. */
    dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, 1);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    packet2_free(packet);
    return 1;
}

static int CTRPS2_ConfigureDebugTextureState(void)
{
    packet2_t *packet;
    qword_t *q;
    clutbuffer_t clut;
    lod_t lod;
    texwrap_t wrap;

    packet = packet2_create(24, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    if (packet == NULL)
        return 0;

    s_debugTexture.info.width = draw_log2(CTRPS2_DEBUG_TEXTURE_WIDTH);
    s_debugTexture.info.height = draw_log2(CTRPS2_DEBUG_TEXTURE_HEIGHT);
    s_debugTexture.info.components = TEXTURE_COMPONENTS_RGBA;
    s_debugTexture.info.function = TEXTURE_FUNCTION_DECAL;

    clut.address = 0;
    clut.psm = 0;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.start = 0;
    clut.load_method = CLUT_NO_LOAD;

    lod.calculation = LOD_USE_K;
    lod.max_level = 0;
    lod.mag_filter = LOD_MAG_NEAREST;
    lod.min_filter = LOD_MIN_NEAREST;
    lod.mipmap_select = LOD_MIPMAP_REGISTER;
    lod.l = 0;
    lod.k = 0.0f;

    wrap.horizontal = WRAP_CLAMP;
    wrap.vertical = WRAP_CLAMP;
    wrap.minu = 0;
    wrap.maxu = 0;
    wrap.minv = 0;
    wrap.maxv = 0;

    q = packet->next;
    q = draw_texture_sampling(q, 0, &lod);
    q = draw_texturebuffer(q, 0, &s_debugTexture, &clut);
    q = draw_texture_wrapping(q, 0, &wrap);
    q = draw_finish(q);
    packet2_update(packet, q);

    dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    draw_wait_finish();
    packet2_free(packet);
    return 1;
}

static int CTRPS2_InitGs(void)
{
    packet2_t *setup;
    qword_t *q;
    int frame_address;
    int z_address;
    int texture_address;

    graph_vram_clear();

    s_frame.width = CTRPS2_FRAME_WIDTH;
    s_frame.height = CTRPS2_FRAME_HEIGHT;
    s_frame.mask = 0;
    s_frame.psm = GS_PSM_16S;
    frame_address = graph_vram_allocate(
        s_frame.width,
        s_frame.height,
        s_frame.psm,
        GRAPH_ALIGN_PAGE);
    if (frame_address < 0)
        return 0;
    s_frame.address = (u32)frame_address;

    /*
     * M3 correctness prototype: keep Z storage allocated but accept every
     * fragment. Depth ordering becomes meaningful only once real track camera
     * and non-coplanar level geometry replace the fixture.
     */
    s_zbuffer.enable = DRAW_ENABLE;
    s_zbuffer.method = ZTEST_METHOD_ALLPASS;
    s_zbuffer.mask = 0;
    s_zbuffer.zsm = GS_ZBUF_16S;
    z_address = graph_vram_allocate(
        s_frame.width,
        s_frame.height,
        s_zbuffer.zsm,
        GRAPH_ALIGN_PAGE);
    if (z_address < 0)
        return 0;
    s_zbuffer.address = (u32)z_address;

    s_debugTexture.width = CTRPS2_DEBUG_TEXTURE_WIDTH;
    s_debugTexture.psm = GS_PSM_32;
    texture_address = graph_vram_allocate(
        CTRPS2_DEBUG_TEXTURE_WIDTH,
        CTRPS2_DEBUG_TEXTURE_HEIGHT,
        GS_PSM_32,
        GRAPH_ALIGN_BLOCK);
    if (texture_address < 0)
        return 0;
    s_debugTexture.address = (u32)texture_address;

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

    CTRPS2_BuildDebugTexture();
    if (!CTRPS2_UploadDebugTexture())
        return 0;
    if (!CTRPS2_ConfigureDebugTextureState())
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
