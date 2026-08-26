# CTR PS2 native renderer

This directory is the PlayStation 2-native renderer target. It does not use SDL,
OpenGL, the desktop VRAM mirror, or the desktop GPU renderer.

## Current milestones

### N0: PS1-free native track path - POTWIERDZONE on real hardware

The default native architecture has already reproduced textured perspective
geometry on a real PlayStation 2:

```text
p2trk immutable geometry streams
  -> DMAC REF
  -> VIF1 UNPACK / TOP/TOPS
  -> VU1 transform + DIV Q
  -> VU1-built GS packet
  -> XGKICK / GIF PATH1
  -> GS
```

The N0 executable does not link the old `ctr_level_bridge`, PS1 VRAM decoder,
`TextureLayout` packer, or desktop renderer. Those remain only as importer and
correctness-oracle research in the repository.

N0 uses native PSMT4 + GS CLUT residency and a fixed prototype 3D camera.

### N1a: native STQ perspective-correct texturing - CURRENT IMPLEMENTATION

N1a changes only the texture-coordinate consumer contract while preserving the
real-hardware validated p2trk geometry streams:

```text
p2trk V3-16 / RGBA8 / V4-16 texel UV
  -> VIF1
  -> VU1 ITOF4
  -> normalize texel UV to ST
  -> transform
  -> DIV Q = 1 / clip.w
  -> S*=Q, T*=Q
  -> PACKED STQ, RGBAQ, XYZ2
  -> XGKICK
  -> GS with FST=0 / PRIM_MAP_ST
```

The source U/V stream remains compact 12.4 texel-space data. N1a supplies
per-material texture normalization as VU batch constants. The current resident
material slot is 64x64; later `p2tex` material descriptors provide dimensions
without hard-coded renderer constants.

Current PS2SDK's draw3d/VU1 sample is the source contract for STQ: compute
`Q=1/w`, multiply normalized S/T by Q, and emit ST before RGBAQ so the GS uses
the correct Q for the vertex.

N1a intentionally keeps conservative VU latency spacing and the existing
correctness barriers. It is not yet a scheduling optimization.

### N1b: reversed opaque depth - CURRENT IMPLEMENTATION, hardware checkpoint pending

N1b enables the GS Z-buffer for the opaque native track pass instead of keeping
an allocated but ALLPASS depth surface.

The current fixture camera emits:

```text
clip.x =  1.15 * x
clip.y = -1.45 * y
clip.z =  1
clip.w =  z
```

so post-divide depth is `1/z`. The VU1 screen transform maps the fixture near
plane (`z=8`) to `0xffff` before XYZ2 emission and leaves farther geometry with
smaller positive Z. The GS is configured for `ZTEST_METHOD_GREATER_EQUAL`,
therefore nearer opaque geometry must win even when it was submitted earlier.

Frame clear deliberately uses the current PS2SDK `draw_disable_tests()` +
`draw_clear()` + `draw_enable_tests()` contract: the clear rectangle writes
Z=0 with ALLPASS, then GEQUAL is restored for geometry.

The fixture contains an adversarial overlap:

```text
near red-quadrant face  -> submitted first
far blue-quadrant face  -> submitted second over nearly the same pixels
```

Expected hardware result: the overlapping region remains the near red texture.
If blue replaces it, the reversed-depth contract has failed. The two lower faces
remain independent STQ/perspective references.

The fixed `z=8` scaling belongs only to this N1b fixture. Production camera
near/far and the final Z16/Z24 choice remain explicit renderer policy and must be
benchmarked against VRAM pressure and precision.

### N1c: multi-cluster asynchronous pass

N1c begins only after N1b reproduces correctly on real hardware. Multiple p2trk
clusters then move into one persistent VIF1 ownership chain:

```text
cluster A REF/UNPACK -> TOP A -> VU1/XGKICK
cluster B REF/UNPACK -> TOP B while VU1 consumes A
cluster C ...
...
one late opaque-pass retirement fence
```

This removes the current per-cluster `submit -> wait` behavior and is the first
whole-pass `submit early, wait late` implementation. BASE/OFFSET/TOPS already
provide the input double-buffer mechanism; output ownership and FINISH placement
remain separate correctness constraints rather than being hand-waved into a
single global wait.

## Current data representation

Static track geometry is already stored in consumer-oriented streams rather than
runtime game structs:

- signed V3-16 position, 6 bytes/vertex;
- RGBA8 color, 4 bytes/vertex;
- V4-16 texcoord transport, with U/V in 12.4 texel space;
- qword-aligned immutable offsets inside p2trk;
- material/pass identifiers instead of PS1 GPU packets or OT links.

VIF1 expands streams directly into VU1-local layout. EE does not gather or widen
static vertices before each draw.

VU1 data memory reserves QW 0..7 for shared constants. VIF1 BASE/OFFSET is
configured once as 8/496, producing two 496-QW TOP/TOPS regions at QW 8 and
QW 504. Current geometry output starts at TOP+96.

## Build

A current PS2DEV/PS2SDK environment is required.

```sh
make -C ps2
```

Output:

```text
ps2/ctr_ps2_renderer.elf
```

With `ps2link`/`ps2client` available:

```sh
make -C ps2 run
```

## Validation policy

For each hardware milestone record at least:

- console SCPH/revision;
- PS2SDK commit;
- EE toolchain version;
- build flags;
- active IRX modules;
- workload / camera / material;
- correctness result;
- VIF bytes and VU1 batch count where relevant;
- PCSX2 result only as a functional cross-check, never as timing authority.

For N1b specifically record whether the near red overlap survives the farther
blue submission and whether any Z fighting, unexpected clipping or wrap appears.

For performance work record p50, p95, p99, max and deadline misses, not only an
average.

The next performance changes happen only after each semantic change is reproduced
on a real PlayStation 2.
