# M2a CTR level bridge benchmark

## Goal

Prove the first source-shaped CTR static-level boundary without carrying the PS1 GTE/POLY/ordering-table renderer into the PS2 hardware path.

Current source contract, verified against this repository's `include/namespace_Level.h` and the matching current `ctr-native` renderer:

- `LevVertex` is 0x10 bytes and begins with three signed 16-bit position components;
- `QuadBlock` begins with `u16 index[9]` at offset 0x00;
- a BSP leaf owns `numQuads` plus `ptrQuadBlockArray`;
- the 1P high-LOD renderer addresses four grid faces through index slots:
  - `{0, 4, 5, 6}`
  - `{4, 1, 6, 7}`
  - `{5, 6, 2, 8}`
  - `{6, 7, 8, 3}`

The PS2 bridge intentionally mirrors only the fields it reads. It does not duplicate the full CTR game structs.

## Current M2a dataflow

```text
CTR-shaped QuadBlock.index[9]
        +
CTR-shaped LevVertex[9]
        |
        | EE gather baseline
        v
4 x V3-16 face streams
        |
        v
DMAC REF -> VIF1 V3-16 UNPACK -> VU1 -> XGKICK -> GS
```

For the first correctness baseline each high-LOD face is submitted independently as a four-vertex GS triangle strip and is followed by the existing FINISH fence.

This is deliberately not presented as the final CTR rasterization contract. Texture selection, draw-order fields, face modes, clipping/subdivision, transparency, water behavior and PS1 quad rasterization equivalence are not implemented by this bridge yet.

## Why keep the runtime gather for now?

It isolates one question: can the current CTR source layout feed the native PS2 geometry path correctly?

It is not the intended steady-state representation. Runtime deindexing duplicates shared grid vertices and burns EE/cache bandwidth. The target asset path is still an offline VIF-ready representation close to the final VU1 consumer.

## Position traffic for one 9-vertex QuadBlock

Current four-face bridge baseline:

- 4 faces x 4 positions = 16 transmitted position records;
- logical V3-16 payload = 16 x 6 B = 96 B;
- each 24 B face payload occupies 2 DMA qwords = 32 B;
- total position DMA payload = 128 B, excluding DMA tags/header traffic.

A future representation that transports the nine unique positions only would need:

- 9 x 6 B = 54 B logical position payload;
- 4 DMA qwords = 64 B after qword rounding;
- plus whatever compact topology/material stream the VU1 program actually needs.

Therefore the next experiment is not "make the gather loop clever". It is to remove duplicated transport and repeated submit/fence overhead by batching the QuadBlock in a representation VU1 can consume directly.

## VU memory bound

The current microprogram stores input positions from TOP+7 and begins output at TOP+64. To avoid the output stream overwriting unread input, the generalized M1/M2a helper currently caps a batch at:

```text
64 - 7 = 57 V3-16 positions
```

This is a correctness bound derived from the current VU memory layout, not a recommended production batch size.

## Coordinate scale

CTR level positions are already signed 16-bit values. The current M1 microprogram uses `ITOF4`, so the eventual CTR camera/object matrix must compensate for that fixed conversion scale instead of rewriting every source position on the EE. The source bytes can remain unchanged.

The fixture uses small coordinates only to keep the present identity-style test matrix visible. It is not retail track geometry and no retail level data is committed to the repository.

## Validation stages

1. Current PS2DEV CI must compile the bridge and VU path.
2. PCSX2 should verify that all four fixture faces reach the GS without VIF/GIF errors or hangs.
3. Real PS2 should reproduce the same output and FINISH completion.
4. Replace the fixture with one loaded retail `QuadBlock` only after the PS2 asset/load boundary exists.
5. Benchmark four-face baseline against one-batch QuadBlock transport.

Real-hardware benchmark metadata must include SCPH/revision, PS2SDK commit, toolchain, flags, active IRX, workload, alignment, buffering, sample count and correctness hash. Record p50/p95/p99/max and deadline misses rather than average alone.

## Next A/B

Baseline A:

```text
4 face gathers
4 VIF submissions
4 VU launches
4 FINISH waits
```

Candidate B:

```text
1 QuadBlock-ready packed stream
1 VIF submission
1 VU launch
1 XGKICK stream
1 correctness fence at the real ownership boundary
```

Candidate B is only a hypothesis until the full face/material semantics fit in one legal VIF/VU/GS stream and reproduce on real hardware.
