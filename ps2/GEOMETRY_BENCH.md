# PS2 VU1 geometry benchmark contract

## Purpose

This benchmark exists to decide renderer representation and scheduling from
end-to-end measurements rather than from isolated instruction folklore.

The first implemented variant is a three-vertex correctness proof. It is not a
performance result.

## Data contract

```yaml
name: geometry_positions_v3_16
producer: PS2 asset-shaped static RDRAM blob
consumer: VIF1 then VU1 then GS
lifetime: persistent for the proof; future track chunks are residency-scoped
representation: signed XYZ, 16-bit each, 12.4 fixed point
allocator_alignment: static object
cache_line_alignment: 64 bytes for the current persistent source
DMA_alignment: 16-byte/qword transfer units
packet_alignment: VIF command/data layout specific
transport: DMAC VIF1 REF
batch_size: parameter; current proof is 3 vertices
deadline: renderer frame deadline
ownership_states: RDRAM_READY -> VIF1_DMA -> VU1_INPUT -> VU1_OUTPUT -> GIF_GS -> RETIRED
```

VIF1 uses signed `V3-16` UNPACK. X/Y/Z are expanded to 32-bit integer fields.
`STMASK + STROW` supplies homogeneous W=1.0 without sending a fourth component.
VU1 then performs `ITOF4.xyz` locally.

## Byte accounting

Logical source representation:

```text
V4-32 baseline: 16 bytes / vertex
V3-16 packed:    6 bytes / vertex
```

Asymptotic source-byte reduction:

```text
1 - 6/16 = 62.5%
```

The current three-vertex proof is constrained by DMA qword transfer size:

```text
V4-32: 3 * 16 = 48 bytes = 3 QW
V3-16: 3 *  6 = 18 bytes -> 32-byte REF = 2 QW
```

So the current physical REF reduction is:

```text
1 - 32/48 = 33.3%
```

Do not report the asymptotic 62.5% number as the measured three-vertex DMA
saving.

## Partial-qword status

The V3-16 proof deliberately zero-pads its referenced source to a whole DMA
qword. The VIF corpus identifies V3-16 final-partial-qword behavior as a case
that deserves controlled real-hardware validation. Therefore:

- **CURRENT IMPLEMENTATION**: source is qword-sized and zero padded;
- **HIPOTEZA DO TESTU**: this exact NUM/source-padding combination behaves as
  intended on the current PS2SDK/toolchain and real VIF1;
- no performance claim follows until correctness is reproduced.

## VU1 memory layout

Shared absolute constants:

```text
QW 0..3   transform matrix
QW 4..7   reserved common constants
```

VIF1 double-buffer configuration:

```text
BASE   = 8
OFFSET = 496
buffer A TOP = 8
buffer B TOP = 504
```

Per active buffer:

```text
TOP+0      scale.xyz + vertex_count.w
TOP+1      screen offset.xyz
TOP+2      primitive GIFtag
TOP+3      RGBA
TOP+4      STQ base
TOP+5..6   FINISH packet
TOP+7..    expanded input positions
TOP+64..   GS-ready VU1 output
```

The fixed output offset is intentionally wasteful in the tiny proof. It makes
input/output ownership obvious and leaves room for the next batch-size sweep.
A final asset format should derive offsets from a bounded batch contract rather
than reserve arbitrary holes forever.

## Current VU1 kernel

The proof executes:

```text
VIF V3-16 expansion
-> ITOF4.xyz
-> 4x4 transform
-> DIV Q
-> perspective divide
-> screen scale/offset
-> FTOI4.xyz
-> ST/RGBAQ/XYZ2 REGLIST construction
-> XGKICK
-> GS FINISH
```

The current instruction schedule intentionally leaves conservative empty slots
around long/dependent operations. It is a correctness baseline.

## Next A/B variants

Batch sizes:

```text
3
6
12
24
48
96
192
```

The general corpus sweep also calls for 8/16/32/64/128/256 vertices; use both
sets where primitive grouping and VU memory capacity permit.

Representation variants:

```text
A: V4-32 float input, no VU integer conversion
B: V3-16 + ROW.W + ITOF4
C: V4-16 where a fourth packed component is genuinely useful
D: mixed streams, positions V3-16 plus later UV/color packing
```

Scheduling variants after correctness:

```text
A: conservative FLUSH + MSCAL baseline
B: double-buffered consecutive batches with no unnecessary pre-submit FLUSH
C: software-pipelined Q schedule
D: Q pipeline + VIF1 double buffering
```

## Measurements

For every real-hardware run record:

```text
SCPH / hardware revision
PS2SDK commit
toolchain version
build flags
active IRX
video mode
representation
vertex count
triangle count
source bytes
DMA qwords
VIF commands/batch
VU1 input qwords
VU1 output qwords
MSCAL count
XGKICK count
FINISH/FLUSH count
sample count
correctness hash
```

Timing output must include:

```text
p50
p95
p99
max
deadline misses
```

Also record whole-frame effects. A locally faster VIF/VU kernel that increases
GIF/GS stalls or Main Bus contention is not a renderer win.
