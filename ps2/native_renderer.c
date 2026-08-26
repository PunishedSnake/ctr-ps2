#include "native_renderer.h"
#include "native_texture_fixture.h"

#include <dma.h>
#include <draw.h>
#include <draw_buffers.h>
#include <draw_sampling.h>
#include <graph.h>
#include <gs_psm.h>
#include <packet2.h>
#include <packet2_utils.h>
#include <tamtypes.h>

#define CTRPS2_NATIVE_TEXTURE_BUFFER_WIDTH 128
#define CTRPS2_NATIVE_CLUT_BUFFER_WIDTH    64

static framebuffer_t s_frame;
static zbuffer_t s_zbuffer;
static texbuffer_t s_texture;
static u32 s_clutAddress;

static u8 s_textureIndices[CTRPS2_NATIVE_TEXTURE_4BIT_BYTES]
    __attribute__((aligned(64)));
static u32 s_textureClut[CTRPS2_NATIVE_TEXTURE_CLUT_ENTRIES]
    __attribute__((aligned(64)));

static int CTRPS2_NativeRendererBuildResidentTexture(void)
{
    return CTRPS2_NativeTextureFixtureBuild(
        s_textureIndices,
        sizeof(s_textureIndices),
        s_textureClut,
        CTRPS2_NATIVE_TEXTURE_CLUT_ENTRIES);
}

static int CTRPS2_NativeRendererUploadResidentTexture(void)
{
    packet2_t *packet;
    qword_t *q;

    packet = packet2_create(48, P2_TYPE_NORMAL, P2_MODE_CHAIN, 0);
    if (packet == NULL)
        return 0;

    q = packet->next;
    q = draw_texture_transfer(
        q,
        s_textureIndices,
        CTRPS2_NATIVE_TEXTURE_WIDTH,
        CTRPS2_NATIVE_TEXTURE_HEIGHT,
        GS_PSM_4,
        s_texture.address,
        s_texture.width);

    q = draw_texture_transfer(
        q,
        s_textureClut,
        CTRPS2_NATIVE_TEXTURE_CLUT_ENTRIES,
        1,
        GS_PSM_32,
        s_clutAddress,
        CTRPS2_NATIVE_CLUT_BUFFER_WIDTH);

    q = draw_texture_flush(q);
    packet2_update(packet, q);

    /* Both transfers contain REF tags. Correctness baseline keeps broad flush. */
    dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, 1);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    packet2_free(packet);
    return 1;
}

static int CTRPS2_NativeRendererConfigureResidentTexture(void)
{
    packet2_t *packet;
    qword_t *q;
    clutbuffer_t clut;
    lod_t lod;
    texwrap_t wrap;

    packet = packet2_create(24, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    if (packet == NULL)
        return 0;

    s_texture.info.width = draw_log2(CTRPS2_NATIVE_TEXTURE_WIDTH);
    s_texture.info.height = draw_log2(CTRPS2_NATIVE_TEXTURE_HEIGHT);
    s_texture.info.components = TEXTURE_COMPONENTS_RGBA;
    s_texture.info.function = TEXTURE_FUNCTION_DECAL;

    clut.address = s_clutAddress;
    clut.psm = GS_PSM_32;
    clut.storage_mode = CLUT_STORAGE_MODE1;
    clut.start = 0;
    clut.load_method = CLUT_LOAD;

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
    q = draw_texturebuffer(q, 0, &s_texture, &clut);
    q = draw_texture_wrapping(q, 0, &wrap);
    q = draw_finish(q);
    packet2_update(packet, q);

    dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    draw_wait_finish();
    packet2_free(packet);
    return 1;
}

static int CTRPS2_NativeRendererInitGs(void)
{
    packet2_t *setup;
    qword_t *q;
    int frame_address;
    int z_address;
    int texture_address;
    int clut_address;

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
     * N1b depth contract. The GS exposes GREATER/GEQUAL rather than the common
     * desktop LESS convention, so VU1 maps nearer geometry to larger positive
     * Z values. Clear writes Z=0, then opaque geometry uses GEQUAL and writes Z.
     *
     * POTWIERDZONE/current PS2SDK: draw_clear() emits Z=0 while tests are put
     * into ALLPASS, and draw_enable_tests() restores zbuffer.method.
     */
    s_zbuffer.enable = DRAW_ENABLE;
    s_zbuffer.method = ZTEST_METHOD_GREATER_EQUAL;
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

    s_texture.width = CTRPS2_NATIVE_TEXTURE_BUFFER_WIDTH;
    s_texture.psm = GS_PSM_4;
    texture_address = graph_vram_allocate(
        CTRPS2_NATIVE_TEXTURE_BUFFER_WIDTH,
        CTRPS2_NATIVE_TEXTURE_HEIGHT,
        GS_PSM_4,
        GRAPH_ALIGN_BLOCK);
    if (texture_address < 0)
        return 0;
    s_texture.address = (u32)texture_address;

    clut_address = graph_vram_allocate(
        CTRPS2_NATIVE_CLUT_BUFFER_WIDTH,
        1,
        GS_PSM_32,
        GRAPH_ALIGN_BLOCK);
    if (clut_address < 0)
        return 0;
    s_clutAddress = (u32)clut_address;

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
        8,
        12,
        22);
    q = draw_enable_tests(q, 0, &s_zbuffer);
    q = draw_finish(q);
    packet2_update(setup, q);

    dma_channel_send_packet2(setup, DMA_CHANNEL_GIF, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    draw_wait_finish();
    packet2_free(setup);
    return 1;
}

static int CTRPS2_NativeRendererConfigureVu1Buffers(void)
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

int CTRPS2_NativeRendererInit(void)
{
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_initialize(DMA_CHANNEL_VIF1, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    dma_channel_fast_waits(DMA_CHANNEL_VIF1);

    if (!CTRPS2_NativeRendererInitGs())
        return 0;
    if (!CTRPS2_NativeRendererBuildResidentTexture())
        return 0;
    if (!CTRPS2_NativeRendererUploadResidentTexture())
        return 0;
    if (!CTRPS2_NativeRendererConfigureResidentTexture())
        return 0;
    if (!CTRPS2_NativeRendererConfigureVu1Buffers())
        return 0;

    return 1;
}

void CTRPS2_NativeRendererClear(u8 r, u8 g, u8 b)
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

void CTRPS2_NativeRendererPresent(void)
{
    graph_wait_vsync();
}
