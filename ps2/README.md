# CTR PS2 renderer bootstrap

This directory is the first PlayStation 2-native execution target.
It does not use SDL, OpenGL, the desktop VRAM mirror, or the desktop GPU renderer.

## Current milestone

The bootstrap validates this hardware path:

```text
EE/RDRAM
  -> DMAC VIF1 chain
  -> VIF1 REF + UNPACK
  -> VU1 TOP/TOPS buffer
  -> VU1 microprogram
  -> XGKICK
  -> GIF PATH1
  -> GS
```

Expected result on a working build is a dark 640x448 display with one Gouraud
triangle. The triangle itself is deliberately trivial. The important result is
that its GIF stream reaches GS through VIF1/VU1/XGKICK rather than direct EE
geometry submission.

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

## What this is not

This is not yet the full CTR executable. The existing desktop/native code is
kept intact while the hardware-native backend is established independently.
Once the VIF1/VU1/GS path is reproduced on real hardware, the next step is to
feed it real CTR geometry through a compact render-command extraction layer.

## Required validation

Record at least:

- console SCPH/revision;
- PS2SDK commit;
- EE toolchain version;
- build flags;
- video mode reported by the display/OSD path;
- visible output correctness;
- whether `FINISH` completes without hang;
- PCSX2 result as a functional cross-check, not timing evidence.

Do not promote this bootstrap to a performance claim until it has run on a real
PlayStation 2.
