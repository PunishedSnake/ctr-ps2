# CTR -> PS2 native render bridge

## Status

This document defines the next integration seam between the decompiled Crash
Team Racing renderer and the PS2-native VIF1/VU1/GS backend.

It deliberately does **not** freeze a permanent render-command ABI yet. The
current CTR source has two materially different producers, static level/BSP
rendering and dynamic RenderBucket geometry. Their final PS2 asset and command
representations must be derived from their real access patterns and validated
before a public structure becomes a compatibility burden.

## Current-source findings

### Static level side

`game/RenderLevel/RenderLists.c` already performs high-level scene decisions:

- BSP traversal;
- leaf visibility and frustum classification;
- visibility-list selection;
- distance/LOD selection;
- render-slot/pool selection;
- linking visible BSP leaves into render lists.

These are gameplay/scene-management decisions and are useful to the PS2 port.
They should not be discarded merely because the final raster backend changes.

### Dynamic RenderBucket side

`game/RenderBucket/RenderBucket_QueueExecute.c` performs substantially more
PS1-specific work after game-side queueing:

- runtime vertex-pool management;
- GTE transforms/projection;
- depth/split decisions;
- material-mode branching;
- PS1 `POLY_*` packet construction;
- ordering-table insertion and depth-derived OT placement;
- per-primitive color/texture state assembly.

The same file also contains substantial thread/scratch/vertex-store machinery
needed by the current native renderer implementation.

### PushBuffer

`game/PushBuffer.c` primarily owns viewport/projection/render-state setup. It is
important for semantic parity, but it is not the first place to attach bulk PS2
geometry transport.

## Probable current bottleneck

**INFERENCE, not yet measured on PS2:** once a PS2 build runs the original CTR
render path, a likely avoidable cost is doing PS1-oriented runtime transform,
splitting and `POLY_*` packet production on the EE only to translate or replace
those packets before they reach GS.

The first PS2 integration should therefore remove work before trying to make
that work faster.

Bad long-term path:

```text
CTR scene decision
 -> GTE/PS1 geometry processing
 -> PS1 POLY_* packets / OT
 -> PS2 translation
 -> GIF/GS
```

Target path:

```text
CTR scene decision
 -> compact PS2 render commands
 -> PS2-native asset batch
 -> DMAC/VIF1
 -> VU1 transform/clip/format
 -> XGKICK
 -> GS
```

## Integration split

### M2a: static level geometry

Use the existing BSP/visibility/LOD result as the producer boundary.

Preserve initially:

- BSP visibility semantics;
- selected leaf/list membership;
- LOD choice;
- game-visible material/texture identity;
- ordering constraints that are required for correctness.

Replace progressively:

- final PS1 GTE screen-space geometry generation;
- PS1 `POLY_*` construction;
- PS1 ordering-table pointer representation;
- runtime repacking into a second PS2 vertex representation.

Desired static asset path:

```text
offline CTR level decode
 -> cluster by render/material/visibility-friendly boundaries
 -> quantize/pack PS2 geometry
 -> emit VIF-ready relative-offset blobs
 -> load/reside
 -> reference directly from frame command stream
```

The first real-level proof should use one bounded leaf/cluster and one material
class. It should not attempt to convert the whole track at once.

### M2b: dynamic RenderBucket geometry

Keep this separate until M2a proves the batch format and VU1 ownership model.
Dynamic objects need additional semantics:

- per-instance transform;
- animation/deformation where applicable;
- material modes;
- split/clipping behavior;
- transparency/order requirements;
- potentially different batch lifetime from static level geometry.

Do not force static level and dynamic object data through an identical record
if their consumers and lifetimes differ.

## Compact command requirements

The first command-stream implementation should carry IDs/offsets and bounded
ranges rather than PS1 OT links or arbitrary object pointers.

Required semantic fields are expected to include, subject to source audit:

```text
geometry/blob reference or relative offset
transform reference/index
material/texture identity
primitive/batch range
LOD/variant
ordering class / sort key where required
render flags
```

The exact widths and field order are intentionally unresolved.

The command stream must have an explicit ownership lifetime:

```text
FRAME_BUILD
 -> READY
 -> VIF1_DMA
 -> VU1_INPUT
 -> VU1_OUTPUT / XGKICK
 -> GIF_GS
 -> RETIRED
```

A frame arena or command page cannot be recycled while any downstream stage can
still read it.

## Ordering-table migration

CTR's PS1 renderer uses ordering-table placement as part of visible behavior,
not just as a pointer-list implementation detail.

Therefore the PS2 port must separate:

```text
ordering semantics
```

from:

```text
PS1 linked OT representation
```

Do not delete depth buckets or reorder transparency simply because GS has a
Z-buffer. Opaque geometry may eventually use more aggressive Z-aware ordering;
semi-transparent/material-special cases need explicit parity tests.

## Material and texture migration

Material identity should become a compact command/asset key rather than a
reason to rebuild large state packets per primitive.

Long-term intent:

```text
material key
 -> prebuilt/persistent GS register block
 -> resident texture/CLUT metadata
 -> geometry batch references material
```

The texture residency system must be designed against the 4 MiB GS local-memory
budget. Static geometry conversion is not permission to upload every CTR texture
at once.

## Geometry representation experiment

M1 currently proves signed V3-16 positions with VIF1 supplying W=1 and VU1
performing ITOF4, transform, perspective, GS formatting and XGKICK.

That format is a candidate for M2a, not a foregone conclusion.

For each real CTR cluster compare at minimum:

```text
V4-32
V3-16 + ROW.W + ITOF4
V4-16 when the fourth component has useful source meaning
mixed packed streams for position/UV/color when semantics are known
```

Do not add normal/color/UV packing until the source fields and required visual
precision are established from actual CTR assets.

## Correctness oracle

The existing native/PS1-shaped renderer remains useful as a semantic oracle.
For the same deterministic camera/frame/asset set, compare:

- visible leaf/object set;
- material/texture IDs;
- primitive counts;
- depth/order class;
- transformed geometry within an agreed tolerance;
- final frame or region hashes where practical.

PCSX2 is suitable for packet inspection and functional iteration. Timing and
subtle VIF/VU/GIF race claims require real hardware.

## Benchmark contract

For every M2 A/B, record the producer-to-final-consumer cost rather than only
VU instruction count.

At minimum:

```text
EE scene/extraction time
commands emitted
command bytes
source geometry bytes
DMA qwords
VIF active/submit-completion proxy
VU1 batch count
XGKICK count/stall proxy
GIF/GS FINISH completion
GS state changes
frame p50/p95/p99/max
deadline misses
correctness hash/result
```

Environment:

```text
SCPH/revision
PS2SDK commit
toolchain
build flags
active IRX
video mode
track/camera/workload
sample count
```

## M2a implementation order

1. Identify the exact post-`RenderLists` level-draw consumer and the stable leaf
   geometry/material fields it reads.
2. Add instrumentation only, with no renderer behavior change, to emit a compact
   trace of one visible leaf/cluster.
3. Build a host/offline converter for that exact geometry representation.
4. Feed one converted cluster to the existing M1 VIF1/VU1 path.
5. A/B the native cluster against the current renderer for visibility, ordering,
   material and screen result.
6. Expand batch size and cluster coverage only after correctness.
7. Re-profile the whole frame because the bottleneck may move to GIF/GS, texture
   upload or scene extraction.

This ordering intentionally removes duplicate PS1-specific runtime work before
attempting VU micro-optimisation.
