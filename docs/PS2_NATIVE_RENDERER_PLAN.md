# CTR PS2 Native Renderer Plan

## Purpose

This document defines the **shipping renderer architecture** for the PS2 port.

The PS1-compatible renderer, PS1 VRAM mirror, `TextureLayout` decoding, ordering-table behavior and `POLY_*` packet generation remain useful as correctness oracles and asset-import semantics. They are **not** intended to become the steady-state PS2 rendering pipeline.

The native target is a dataflow renderer designed around the PlayStation 2 hardware:

```text
CTR gameplay / scene semantics
          |
          v
scene snapshot / visibility
          |
          v
compact PS2 render commands
          |
          +-------------------------------+
          |                               |
          v                               v
   resident PS2 assets              async asset jobs
          |                         IOP / IPU / DMAC
          v                               |
     DMA REF chains                       v
          |                         ready texture/data
          v                               |
         VIF1 <---------------------------+
          |
          v
         VU1
          |
       XGKICK
          |
       GIF PATH1
          |
          v
          GS
```

The renderer is not an OpenGL compatibility layer and is not a PS1 GPU emulator.

---

## Evidence labels

- **POTWIERDZONE**: manual, current source, or real-hardware reproduction.
- **CURRENT IMPLEMENTATION**: behavior of the current branch/toolchain.
- **INFERENCJA**: architectural conclusion not yet measured end-to-end.
- **HIPOTEZA DO TESTU**: optimization requiring a real-hardware A/B benchmark.

---

# 1. Design rules

1. Remove PS1-only work before optimizing it.
2. Keep gameplay semantics, not PS1 GPU implementation details.
3. Static assets are converted offline to final-consumer-ready PS2 formats.
4. EE must not transform or repack static vertices every frame.
5. VIF1 expands packed geometry directly into VU1-local compute layout.
6. VU1 emits final GIF/GS packets through XGKICK.
7. Opaque geometry uses the GS Z-buffer instead of emulating the PS1 ordering table.
8. Transparent/special passes preserve only the ordering semantics actually required by the material.
9. Texture residency is explicit. Upload once, reuse, evict deliberately.
10. IPU is used only for workloads matching its fixed-function operators.
11. All asynchronous subsystems use explicit buffer ownership and `submit early, wait late`.
12. Real PS2 timing decides optimization choices.

---

# 2. Runtime scene boundary

The renderer should eventually consume a PS2-owned snapshot instead of PS1 primitives.

Conceptually:

```c
struct CTRPS2SceneSnapshot
{
    CameraState camera;

    VisibleStaticClusters static_clusters;
    DynamicInstanceCommands instances;
    SpriteCommands sprites;
    EffectCommands effects;
    UiCommands ui;

    ResidencyRequests textures;
};
```

Exact ABI remains intentionally undefined until the first real track and dynamic-model integrations are complete.

The important boundary is semantic:

```text
GAME SIDE
- camera
- visibility
- LOD choice
- instance state
- animation state
- material/effect identity

RENDERER SIDE
- batching
- texture residency
- VIF stream selection
- VU microprogram selection
- Z / alpha / fog / blend state
- GIF packet production
- GS submission
```

There should be no `POLY_GT4`, PS1 OT link, host PS1 VRAM pointer or retail pointer-byte trick in the final low-level renderer command stream.

---

# 3. Native PS2 asset compiler

## 3.1 Input

The importer may consume original CTR data semantics:

- LEV/BSP/QuadBlock geometry;
- model data;
- VRM texture data;
- `TextureLayout`;
- vertex colors;
- source material/transparency flags;
- animation data;
- original LOD/visibility metadata.

This is a **build-time compatibility layer**.

## 3.2 Output packages

Suggested shipping package classes:

```text
track.p2trk
models.p2mdl
textures.p2tex
ui.p2ui
video.p2vid
sound.p2aud
```

A track package should contain approximately:

```text
Track header
BSP/visibility mapping
cluster bounds
cluster -> material list
VIF-ready geometry blocks
material table
texture handles/residency groups
optional prebuilt state/DMA templates
cold/debug metadata stripped from shipping build
```

## 3.3 Static geometry representation

Primary candidate:

```text
per batch header
    material id
    vertex count
    topology
    bounds
    scale/bias

packed vertex stream
    s16 position[3]
    s16 ST[2] or another measured compact UV representation
    u8  RGBA[4]
    optional normal/effect fields only when consumed
```

VIF reconstructs VU-local `[x y z 1]`, widened color and other constants.

Do not store repeated W/default alpha/per-vertex material IDs when they can be supplied once per batch.

## 3.4 Track clustering

The original QuadBlock layout remains useful as source topology, but it is not the final submission granularity.

Offline compiler work:

```text
source QuadBlocks
 -> classify opaque / cutout / translucent / special
 -> resolve source texture/material semantics
 -> remove PS1-only subdivision when perspective-correct STQ makes it unnecessary
 -> cluster spatially and by material
 -> generate strips/lists where they reduce transport without harming GS page behavior
 -> quantize
 -> emit VIF-ready blocks
```

**INFERENCJA:** Much of the PS1 runtime subdivision used to control affine-texture distortion can disappear from the PS2 renderer once 3D geometry uses perspective-correct STQ. This must be checked against every subdivision use before removal.

---

# 4. Frame pipeline

Target high-level frame schedule:

```text
IOP: stream future asset/audio chunks ------------------------------+
                                                                    |
IPU: optional large-image/video/texture decode in available slack --+
                                                                    |
EE: simulation N                                                    |
EE: BSP / scene extraction                                          |
EE: build compact command snapshot                                  |
                                                                    |
PATH3: early missing texture/state uploads -------------------------+
                                                                    |
VIF1: unpack static batch B             ||                          |
VU1 : process static batch A            || EE prepares later work   |
GIF : consume previous XGKICK           ||                          |
GS  : raster previous packet            ||                          |
                                                                    |
repeat opaque world / instances / effects                           |
                                                                    |
transparent ordered passes                                          |
UI                                                                  |
late dependency fence                                               |
present                                                             |
```

The final renderer should not use one `FINISH` per face, QuadBlock or ordinary draw call.

---

# 5. EE / R5900 role

The EE owns control-heavy work:

- game simulation;
- race logic;
- AI;
- BSP traversal;
- high-level visibility;
- command extraction;
- material/pass classification;
- texture residency decisions;
- asynchronous scheduler control.

The EE should **not** perform:

- per-vertex static track transforms;
- PS1 GTE emulation for rendering;
- PS1 primitive construction;
- PS1 OT construction for opaque geometry;
- per-frame static asset repacking;
- large sequential copies that can be removed or moved by DMA.

A frame command should mostly contain references and small dynamic constants.

---

# 6. VU0 role

VU0 is a secondary compute resource, not mandatory decoration.

Candidate native uses:

- short matrix/vector helpers in macro mode;
- regular animation preparation;
- particle/effect simulation kernels;
- dense post-BSP visibility/LOD math;
- selected collision or normalization kernels shared with gameplay.

**HIPOTEZA DO TESTU:** a VU0 micro-mode pass over a dense cluster-bounds array may reduce EE visibility math when enough independent EE work exists to overlap it. BSP pointer traversal itself should remain on the EE unless profiling proves otherwise.

Do not move branch-heavy object graphs or AI to VU0.

---

# 7. VIF1 role

VIF1 is the geometry-stream constructor.

It should receive already-packed asset blocks through DMAC REF chains and perform:

- UNPACK;
- widening;
- constant fill through ROW/COL/masks where appropriate;
- optional measured compact-format reconstruction;
- hardware input double buffering through BASE/OFFSET/TOP/TOPS.

The final steady-state path should resemble:

```text
RDRAM immutable VIF block
    |
DMA REF
    |
VIF1 unpack while VU1 consumes previous input
    |
VU1
```

Runtime EE gathering of individual vertices into temporary float arrays is considered architectural debt.

---

# 8. VU1 microprogram families

Do not force every renderer workload through one enormous microprogram.

## 8.1 Static track microprogram

Inputs:

- quantized positions;
- UV/ST;
- vertex color;
- view/projection constants;
- material constants.

Work:

- transform;
- clip/cull policy;
- reciprocal/Q scheduling;
- perspective-correct STQ;
- optional fog value;
- GS fixed-point conversion;
- GIF packet generation;
- XGKICK.

Static track world transform is normally identity, so per-batch work should not pretend every track cluster is a dynamic object.

## 8.2 Dynamic instance microprogram

For karts, props and moving objects:

- object/world/view/projection;
- measured animation/deformation work that maps well to VU1;
- lighting/effect calculations;
- GS packet generation.

Exact animation strategy requires a source audit before choosing skinning, morph or another representation.

## 8.3 Sprite/particle microprogram

Instead of sending four complete vertices per sprite:

```text
center / size / rotation / color / UV frame
        |
       VU1
        |
expand billboard geometry
        |
      XGKICK
```

**HIPOTEZA DO TESTU:** this can reduce EE vertex generation and Main Bus bytes for large particle/sprite batches.

## 8.4 Water/effect microprograms

Keep separate only when the workload is regular enough to justify a specialized program.

Possible uses:

- UV animation;
- wave/deformation math;
- fog/effect coordinates;
- procedural strip generation.

---

# 9. GS-native material system

## 9.1 Stop emulating the PS1 ordering table

For opaque world and opaque instances:

```text
GS Z-buffer = visibility/order correctness
```

This removes a large class of PS1-specific OT construction and depth-bias behavior from the normal path.

The source `order_bias` remains compatibility metadata only where a material/effect genuinely depends on draw order.

Transparent content is handled through explicit native passes:

```text
opaque
cutout / alpha-test
special masked effects
transparent / blended ordered buckets
additive effects
UI
```

## 9.2 Perspective-correct texture mapping

3D geometry should use STQ rather than preserving PS1 affine mapping artifacts.

This is one of the key PS2-native visual upgrades and may allow removal of PS1 geometry subdivision whose only purpose was reducing affine distortion.

UI and deliberately screen-space primitives can continue to use UV/FST where appropriate.

## 9.3 Fog

Use GS fixed-function fog where the desired CTR effect maps cleanly to it, with VU1 producing the interpolated fog parameter.

## 9.4 Blend/mask effects

Use GS ALPHA/TEST/FRAME-mask/multipass operations when they replace CPU/VU work and fit the source visual semantics.

Do not create multipass effects merely because the GS can do them. Fullscreen RMW and local-memory page traffic remain budgeted costs.

---

# 10. Texture system and GS local-memory residency

The renderer owns a compact texture descriptor approximately containing:

```text
width / height
PSM
CLUT PSM / handle
VRAM block requirement
package offset
residency group
state: NONRESIDENT / UPLOADING / RESIDENT
last-use / priority metadata
```

Preferred asset classes:

```text
PSMT4 + CLUT   small/paletted track and UI textures
PSMT8 + CLUT   textures needing larger palettes
PSM16          higher-quality opaque/color textures
PSM32          only when the visual/precision benefit earns the VRAM cost
```

Textures should be grouped offline by likely spatial/material use so storage locality helps residency locality.

Runtime rule:

```text
upload once -> draw many -> evict deliberately
```

not:

```text
upload per draw
```

Mip/LOD selection should be designed around GS Texture Page Buffer behavior and the actual screen-space footprint.

---

# 11. PATH1 / PATH3 schedule

Use:

```text
PATH1 = VU1 XGKICK geometry
PATH3 = texture uploads + selected static/state packets
```

PATH3 transfers must be chunked so they do not monopolize GIF while VU1 has geometry ready.

Potential later tuning:

- EOP granularity;
- intermittent PATH3 scheduling;
- pre-uploading likely residency one or more frames early.

These remain real-hardware tuning problems.

---

# 12. IPU: useful native role, not fake extra GPU

## 12.1 What IPU should not do

Do not route ordinary geometry, lighting or arbitrary image kernels through IPU.

IPU is fixed-function and shares Main Bus/DMAC bandwidth. It cannot replace GS rasterization and does not make GS work disappear.

## 12.2 Tier A: mandatory natural use

### FMV / STR / MPEG

Cutscenes and other MPEG-like media should use the native IPU decode path.

This removes substantial video decode work from EE and is the workload the hardware was built for.

## 12.3 Tier B: large texture/image streaming experiment

For large RGB texture classes, loading screens or enhanced assets, benchmark:

```text
raw PSM16 asset
       vs
intra-MPEG compressed asset
 -> toIPU DMA
 -> IDEC / CSC output
 -> fromIPU DMA
 -> GS upload
```

Potential benefit:

- lower storage/SIF traffic;
- EE does not perform general image decode;
- fixed-function output can be RGB16.

Potential loss:

- MPEG artifacts;
- macroblock granularity;
- extra Main Bus/DMAC traffic;
- contention with VIF1/GIF;
- decode buffers.

Only asset classes that win end-to-end should use this path.

## 12.4 Tier C: PACK/VQ experiment

IPU PACK can assign RGB data to a supplied 16-color codebook and emit INDX4.

Possible native use:

```text
runtime/generated RGB image
       |
IPU PACK/VQ
       |
INDX4 + reused 16-color palette
       |
GS PSMT4-compatible asset path
```

For static CTR textures this is usually inferior to offline PSMT4 generation, because doing at runtime would reintroduce work that can be removed completely.

It becomes interesting only for genuinely dynamic images or when the input already exists as RGB and a supplied palette is appropriate.

## 12.5 Scheduling policy

IPU jobs are background/async jobs with explicit ownership:

```text
FREE
 -> TO_IPU_DMA
 -> IPU_ACTIVE
 -> FROM_IPU_DMA
 -> READY_FOR_GS
 -> GS_UPLOAD
 -> RESIDENT
```

Run utility IPU jobs in slack where possible. They must not blindly overlap the heaviest VIF1/GIF/SIF traffic merely to keep the IPU busy.

---

# 13. Scratchpad / DMAC

Scratchpad is a bounded local store, not a universal cache replacement.

Candidate uses:

- small command sort tiles;
- VU0/MMI work blocks;
- IPU/EE staging when a measured pipeline naturally uses SPR;
- short-lived streaming metadata blocks.

Static geometry should normally be referenced directly by DMA rather than copied through SPR without a measured reason.

DMAC carries bulk data. Each renderer stream needs explicit ownership and cache coherency rules.

---

# 14. IOP / SIF / SPU2 integration

The renderer is only one real-time client of the machine.

IOP responsibilities:

- storage reads;
- controller/device service;
- filesystem/service work;
- audio service.

SIF:

- control through RPC/CMD;
- bulk asset/audio data through bounded DMA/rings when appropriate.

SPU2:

- PS-ADPCM playback;
- resident short samples;
- streamed music/long audio with deadline telemetry.

Renderer scheduling must leave audio deadlines safe under worst-case texture/asset streaming.

---

# 15. Native render passes

Initial target pass structure:

```text
1. clear / frame setup
2. sky/background
3. opaque static track
4. opaque dynamic instances
5. cutout geometry
6. water/special surfaces
7. transparent geometry/effects
8. particles/sprites
9. UI
10. presentation
```

This is intentionally not the PS1 OT replay order.

Exact pass split must be validated against source material semantics and screenshots from the compatibility renderer.

---

# 16. Migration from the current M3 prototype

The current M3 path is useful because it proves hardware contracts and source decoding. It should evolve as follows.

## N0: M3 correctness bridge

Already established/progressing:

```text
QuadBlock source layout
TextureLayout source semantics
retail UV rotation
PS1 VRM decode oracle
VIF1/VU1/GS textured geometry
PSMT4/CLUT experiments
```

Purpose: prove input semantics and hardware path.

## N1: real asset import

- load one real LEV/VRM source pair;
- select one known QuadBlock;
- resolve its real texture/material;
- render it through the current backend;
- compare with compatibility renderer.

## N2: first offline native track block

- host-side converter writes a small `p2trk` asset;
- PS2 runtime no longer reconstructs a PS1 VRAM mirror for that asset;
- geometry is already VIF-ready;
- texture is already GS-ready PSM/CLUT;
- one native material descriptor replaces PS1 tpage/clut interpretation.

This is the real architecture cutover point.

## N3: opaque track renderer

- convert a complete BSP leaf or small track region;
- use native Z-buffer ordering;
- perspective-correct STQ;
- material clustering;
- multiple VIF batches;
- no PS1 primitive generation.

## N4: full static track

- native package for all track geometry;
- explicit residency groups;
- BSP -> cluster mapping;
- larger draw distance experiments;
- GS page/overdraw profiling.

## N5: dynamic objects

- kart/prop model asset compiler;
- dedicated VU1 instance path;
- native material system;
- animation strategy after source audit.

## N6: effects

- sprites/particles;
- water;
- shadows;
- transparency;
- fog;
- sky;
- UI.

## N7: asynchronous whole-system renderer

- triple-buffered frame snapshots/command arenas;
- VIF1 input/output buffering;
- PATH1/PATH3 tuned scheduling;
- IOP asset read-ahead;
- optional IPU texture/media jobs;
- SPU2 deadlines instrumented;
- broad correctness fences reduced to dependency-correct fences.

---

# 17. What gets deleted from the PS2 hot path

The following are migration scaffolding, not final renderer features:

```text
PS1 1024x512 VRAM mirror for ordinary rendering
PS1 primitive packet construction
PS1 GTE renderer transform path
opaque ordering-table construction
pointer-byte ordering tricks
runtime TextureLayout interpretation for converted assets
runtime static geometry repacking
per-face FINISH
per-draw texture upload
```

They may remain in tools, debugging, validation, or compatibility builds.

---

# 18. Benchmark gates

Every architecture step must report end-to-end numbers on real hardware.

At minimum:

```text
frame p50 / p95 / p99 / max
visible clusters
submitted vertices/triangles
VIF source bytes
VU1 batches
XGKICK stalls / proxy measurements
PATH3 upload bytes
GS FINISH time
VRAM resident bytes
texture evictions
EE copied bytes
EE renderer-build time
IPU bytes/jobs when active
SIF/storage bytes
SPU2 underruns / minimum occupancy
```

A change is not accepted merely because one subsystem got faster if frame p99, audio deadlines or bus contention got worse.

---

# 19. Target end state

The intended final renderer is:

```text
CTR simulation
      |
      v
semantic scene snapshot
      |
      +-------------------------------+
      |                               |
      v                               v
opaque/special render commands    async resource scheduler
      |                               |
      v                         IOP / IPU / DMAC
PS2-native asset handles               |
      |                               v
      +-----------> residency / ready resources
      |
      v
DMA REF -> VIF1 -> VU1 -> XGKICK -> GIF -> GS
                                      |
                                  native Z
                                  native STQ
                                  native fog/blend
                                  explicit VRAM cache
```

The PS1 renderer remains a visual oracle and importer reference.

The shipping PS2 renderer should no longer *think like a PS1 GPU*.
