# CTR PS2 Native Architecture

## Status

This document defines the target architecture for the PlayStation 2 port.
It is intentionally hardware-native. The desktop PS1 GPU/GTE emulation path
remains useful as a correctness oracle, but it is not the final PS2 renderer.

The first hardware bootstrap lives in `ps2/` and validates the transport path:

```text
EE packet/data producer
        |
        v
      DMAC
        |
        v
      VIF1
        |
   UNPACK + TOP/TOPS
        |
        v
       VU1
        |
     XGKICK
        |
        v
       GIF
        |
        v
       GS
```

The bootstrap initially sends a ready GIF packet through VU1. The next stage
replaces that packet with packed CTR geometry transformed and emitted by VU1.

## Evidence labels

Architecture and optimization changes use these labels:

- **POTWIERDZONE**: hardware manual, current source, or real-hardware reproduction.
- **CURRENT IMPLEMENTATION**: behavior of the PS2SDK/toolchain revision being used.
- **HISTORYCZNE**: old SDK/toolchain/forum behavior, useful as context only.
- **INFERENCJA**: architectural conclusion not yet measured on real hardware.
- **HIPOTEZA DO TESTU**: candidate optimization that requires an A/B hardware benchmark.

## Optimization policy

The renderer and engine follow this order:

1. remove unnecessary work;
2. perform work less often;
3. reduce data volume;
4. improve data layout and locality;
5. batch work;
6. remove copies and dynamic allocation from hot paths;
7. overlap producer and consumer work;
8. use specialized PS2 hardware;
9. hand-specialize only measured hot kernels.

Every major stream must define:

```text
producer
consumer
lifetime
representation
alignment
transport
batch size
deadline
ownership states
```

The engine uses `submit early, wait late`. Double/triple buffering is an
ownership model, not a decorative array of buffers.

## Hardware ownership

### EE / R5900

Primary responsibilities:

- game simulation;
- race rules and timers;
- AI/control flow;
- visibility extraction;
- render command extraction and sorting;
- whole-system scheduling;
- high-level asset and streaming decisions.

The EE should not repack every vertex into a VU-friendly float4 format every
frame. Static geometry should arrive from the asset pipeline in a format that
VIF1 can unpack directly into the VU1 working representation.

VU0/MMI are candidates only for measured regular kernels. Branch-heavy object
walking remains on the EE until profiling says otherwise.

### Scratchpad

The 16 KiB EE Scratchpad is reserved for bounded hot working sets where a
measured tile/staging design beats cached RDRAM.

Candidate uses:

- short-lived transform/collision tiles;
- streaming staging blocks;
- tightly bounded MMI/VU0 kernels;
- command extraction scratch data.

No global `ALIGN(64)` policy follows from Scratchpad or cache-line size.
Allocator, cache-line, DMAC, SIF and device alignment remain separate contracts.

### DMAC

Bulk movement is a DMAC responsibility.

Target traffic includes:

- RDRAM -> VIF1 geometry streams;
- RDRAM -> GIF PATH3 texture/state uploads;
- SPR staging transfers;
- IPU input/output;
- SIF bulk rings.

The target is minimum unhideable transport time, not maximum isolated channel
bandwidth. Large bursts must not blindly collide with audio, SIF, IPU and
renderer deadlines on the shared machine.

### VIF1

VIF1 is the renderer's data formatter/decompressor.

Target asset representation:

```text
PS2 asset compiler
    -> material/mesh clustering
    -> quantization
    -> strip/index preparation where beneficial
    -> VIF-ready packed blocks
    -> runtime bulk read
    -> VIF1 UNPACK
    -> VU1 local working set
```

The VIF1 BASE/OFFSET/TOP/TOPS mechanism owns the geometry double buffer.
The current bootstrap reserves two equal VU1 data-memory regions. Exact batch
sizes will be tuned once real CTR geometry is flowing.

Packed formats to benchmark include V4_32, V3_32, V4_16 and mixed 16/8-bit
streams. No format wins by ideology. Measure bytes transported, VIF time, VU
cycles and end-to-end geometry completion.

### VU1

VU1 is the primary geometry processor.

Target responsibilities:

- object/world/view/projection transforms;
- clipping and culling work that maps well to regular batches;
- reciprocal/Q scheduling;
- perspective-correct STQ generation;
- fixed-point GS coordinate conversion;
- selected lighting/effect math;
- construction of final GS-ready GIF packets;
- XGKICK to PATH1.

The desired terminal form is:

```text
packed geometry
  -> VIF1
  -> VU1 transform/clip/format
  -> GS-ready packet in VU memory
  -> XGKICK
  -> GIF
  -> GS
```

Transformed vertices should not return to the EE merely to be repacked.

The bootstrap microprogram currently only executes `xtop` + `xgkick`. That is a
transport/correctness proof, not the final geometry kernel.

### VU0

VU0 is not a consolation prize for code that did not fit elsewhere.

Candidate workloads after profiling:

- matrix batches;
- vector normalization or collision math;
- regular particle/effect transforms;
- selected animation math.

Pointer chasing, complex branches and control-heavy AI stay on the EE.

### GIF

PATH1 is the preferred geometry path from VU1.

PATH3 remains useful for:

- texture uploads;
- larger static/state packets;
- framebuffer setup/clears where appropriate.

PATH scheduling must prevent bulk PATH3 work from starving time-critical PATH1
geometry. Broad FINISH/FLUSH usage is a baseline correctness tool, not the final
synchronization strategy.

### GS

The GS is the final rendering consumer, not an OpenGL-shaped abstraction.

The 4 MiB local-memory budget must explicitly include:

```text
framebuffers
z buffer
resident textures
CLUTs
transient render targets
effect scratch/residency
```

Initial bootstrap policy uses a 640x448 `PSMCT16S`-class framebuffer and 16-bit
Z storage through current PS2SDK PSM constants. This is a budget-conscious
baseline, not a universal final mode.

Final renderer policy:

- texture residency is explicit;
- PSMT4/PSMT8 + CLUT are first-class formats where quality allows;
- 16-bit textures/framebuffers are considered where they release meaningful VRAM;
- 32-bit storage is used where precision/quality earns its cost;
- material sorting reduces state churn;
- page behavior and overdraw are measured;
- multipass effects have explicit bandwidth and residency budgets;
- mip/LOD policy is GS-aware rather than copied from modern PC assumptions.

### IOP

The IOP is a service CPU, not free compute.

Responsibilities:

- controller/memory-card services;
- storage/device services;
- audio service support;
- network/device drivers;
- bounded asynchronous command processing.

Heavy floating-point geometry, gameplay AI and generic decompression should not
be pushed to the IOP just to claim another processor is busy.

### SIF

SIF RPC/CMD is primarily control plane.

Bulk data should use bounded DMA/ring ownership where the active subsystem
supports it. Small synchronous RPC calls in hot loops are forbidden unless a
measurement proves they are irrelevant.

### SPU2

Audio target:

- SPU2-native voice playback;
- PS-ADPCM assets prepared offline;
- stable resident samples where practical;
- streaming rings for long music/voice data;
- deadline-aware refill telemetry.

Audio correctness is measured with minimum occupancy, underruns, p95/p99 refill
latency and worst-case contention, not average throughput alone.

### IPU

IPU is fixed-function, not a general shader/vector processor.

Primary target use:

- MPEG/STR/cutscene decode where appropriate;
- measured fixed-function CSC/VQ/PACK/FDEC experiments;
- texture/decode pipelines only when the final representation avoids pointless
  round trips through EE cache.

Any non-video IPU use is a hypothesis until its setup, DMA, bus pressure and
consumer cost beat the EE/MMI alternative on real hardware.

### Storage / DEV9 / USB / network

Runtime asset storage should favor large sequential containers and read-ahead.

During gameplay:

- background reads use bounded chunks;
- producer rings carry explicit ownership;
- audio/input/render deadlines outrank background streaming;
- HDD/DEV9/SIF bursts are scheduled around renderer pressure where useful.

USB is a compatibility/development source, not the preferred high-bandwidth
streaming path. Network transport is useful for development/telemetry and
potential content streaming only after its own measured budget is known.

## Renderer data model

The target runtime does not traverse arbitrary game object graphs while
building low-level GS state.

```text
game state
   |
visibility extraction
   |
compact render commands
   |
material / pass buckets
   |
static PS2-native asset references + dynamic constants
   |
VIF1 DMA chains
   |
VU1
   |
GS-ready packets
```

Static data should already be close to its final consumer representation.
Runtime conversion is reserved for genuinely dynamic data.

## Ownership states

The initial geometry stream evolves toward this ownership model:

```text
FREE
 -> EE_BUILDING_COMMANDS
 -> READY_FOR_DMA
 -> DMAC_VIF1_OWNS
 -> VU1_INPUT_OWNS
 -> VU1_OUTPUT_OWNS
 -> GIF_GS_OWNS
 -> RETIRED
 -> FREE
```

A frame/geometry arena cannot be reused until the last consumer is finished.
`free()` or an arena reset is not synchronization.

## Synchronization baseline

The first bootstrap deliberately uses current PS2SDK's convenience
`packet2_utils_vu_add_start_program()`, which emits `FLUSH + MSCAL`.

Classification:

- **CURRENT IMPLEMENTATION**: this helper is a safe/simple PS2SDK submission path.
- **NOT A HARDWARE CLAIM**: the renderer does not assume every MSCAL requires a
  broad FLUSH forever.
- **HIPOTEZA DO TESTU**: once double-buffer ownership is proven, replace broad
  barriers with the narrowest dependency-correct VIF/VU/GIF synchronization.

The same rule applies to GS FINISH. Wait at the latest correctness deadline,
not immediately after every submission.

## Development milestones

### M0 - hardware transport proof

Current branch target:

- PS2DEV EE build;
- GS framebuffer/Z allocation;
- GIF drawing environment;
- VU1 microprogram upload;
- VIF1 BASE/OFFSET double-buffer contract;
- REF + UNPACK of persistent GIF data;
- MSCAL;
- VU1 `xtop` + `xgkick`;
- GS FINISH fence;
- visible triangle on real PS2.

### M1 - transformed VU1 geometry

- packed input vertices;
- matrix constants resident in VU1;
- VU1 transform;
- divide/Q scheduling;
- perspective STQ;
- clipping policy;
- GS-ready output built by VU1;
- first A/B batch-size sweep.

### M2 - CTR render-command extraction

- convert CTR visibility/render buckets into compact PS2 commands;
- keep desktop renderer as correctness oracle;
- feed real track/instance geometry to M1 backend;
- eliminate PS1 VRAM-mirror dependency from PS2 path.

### M3 - texture/CLUT residency

- map CTR texture pages to GS-native residency;
- PSMT4/PSMT8/PSM16 choices by asset class;
- CLUT cache;
- batched uploads;
- texture lifetime/eviction telemetry.

### M4 - full frame ownership

- double/triple frame command arenas;
- submit early, wait late;
- overlap EE gameplay/visibility with VIF1/VU1/GS work;
- remove unnecessary broad barriers;
- instrument PATH1/PATH3 pressure.

### M5 - system services

- libpad input;
- memory card;
- storage streaming service;
- SPU2 audio;
- cutscene/IPU path;
- optional HDD/network development services.

### M6 - remove PS1-era limits deliberately

Only after the PS2 path is measurable:

- larger memory budgets;
- increased draw distance/visibility budget;
- higher object/particle budgets;
- widescreen;
- higher presentation modes where useful;
- improved texture assets;
- 60 Hz presentation/interpolation experiments;
- simulation-rate changes only after timing/gameplay audit.

## Benchmark contract

Every serious optimization result records at least:

```text
SCPH / hardware revision
PS2SDK commit
toolchain version
build flags
active IRX modules
video mode
workload / track / camera
player count
batch format
batch size
alignment/cache policy
buffering mode
sample count
correctness hash
```

Frame and subsystem timing reports include:

```text
p50
p95
p99
max
deadline misses
```

Renderer-specific counters should additionally track:

```text
visible commands
triangles/vertices submitted
VIF bytes
VU1 batches
MSCAL count
XGKICK count
GIF/PATH packet bytes
texture upload bytes
VRAM resident bytes
state changes
FINISH/FLUSH waits
EE copied bytes
```

PCSX2 is a correctness/debug lane. Performance, cache, DMA, FIFO and subtle
race conclusions require a real PlayStation 2.
