# Validation

Recorded on 18/11/2026, Windows 11, MSVC 2022, Release build.

## Automated checks

- CTest: 5/5 (`test_m68k_core`, `test_z80`, `test_neogeo`, `test_video`, `test_sprites`). These are hand-assembled programs and synthetic fixtures, not full instruction-set coverage.
- Save states: `test_savestate` on 13 games — the state saved at frame N and loaded into a different instance produces identical frames, sound, WRAM, VRAM, palette, Z80 RAM and cycle count for the following frames; a state from another game is refused.

## Comparisons against MAME 0.289

- Boot frame 200, 155 games: screenshots identical pixel for pixel.
- Health sweep, 170 romsets × 3000 frames: 168 reach the attract mode; `powerins` and `sbp` do not load (not standard MVS sets). Compared with the previous sweep, no game regressed; fatfury3, samsho3, viewpoin, mutnat and b2b went from black screen or freeze to running.
- b2b, roboarmy, joyjoy, svc at frame 2999: same image as MAME (b2b and roboarmy differ only in attract timing and blinking text).
- Cycle offset against MAME (68000 `totalcycles` aligned on the BIOS vblank handler): b2b −10 cycles at frame 200 and 0 at frame 400.
- Sound: YM2610 register traces replayed through the original ymfm match our output.

## Limits

- No game was played through to the end as part of this validation.
- The IRQ2 raster timer access-time detail measured in Achado #101 has no known visible effect left, but is not modeled at sub-instruction precision.
- The Linux ARM port (Fase 8) has not started.

## Public source import check — 18 November 2026

The public source snapshot was built separately on Windows x64 with GCC 16.2.0, CMake and Ninja, in Release mode. All five synthetic CTest checks passed: `test_m68k_core`, `test_z80`, `test_neogeo`, `test_video` and `test_sprites`.

All emulator source code, brand assets, credits and license files were preserved byte for byte. Public-facing documentation and ignore rules were adapted for the public repository; private collaboration notes and private Git history were not imported.

This check builds the core and test programs. It does not build or validate the SDL2 frontend or launcher, rerun the ROM-set sweep above, or establish full-game compatibility. The build completed with existing GCC warnings about indentation, possible path truncation and miniz integer ranges; no source changes were made to suppress them.
