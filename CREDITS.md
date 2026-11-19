# Credits

zera330 is written by the Zera group and maintained by **metallrockdev**. The emulation core is original C code; the parts below come from other projects or were transcribed from them, and keep their licenses.

## Code included in the repository

| Component | Where | License |
| --- | --- | --- |
| 68000 and Z80 CPU cores, ported from the author's STM32F429 Mega Drive project | `core/src/cpu/` | MIT, [licenses/m68k-z80-MIT.txt](licenses/m68k-z80-MIT.txt) |
| miniz (ZIP reading) | `third_party/miniz/` | MIT, [licenses/miniz-MIT.txt](licenses/miniz-MIT.txt) |

## Parts transcribed from reference sources

These pieces were transcribed from the original source code to reproduce the hardware exactly. Each file names its source in the header comment.

| Source | Used in | License |
| --- | --- | --- |
| ymfm by Aaron Giles (ADPCM-A/B algorithm and tables) | `core/src/audio/ym2610.c` | BSD-3-Clause, [licenses/BSD-3-Clause.txt](licenses/BSD-3-Clause.txt) |
| MAME: SMA and CMC42 decryption and bank tables | `core/src/system/prot_sma_kof99.c` | BSD-3-Clause |
| MAME: CMC50 decryption tables | `core/src/system/prot_cmc50.c`, `cmc50_tables.inc` | BSD-3-Clause |
| MAME: µPD4990A real-time clock | `core/src/system/upd4990a.c` | BSD-3-Clause |
| MAME: sprite and fix-layer drawing rules | `core/src/video/lspc_sprites.c`, `lspc_fix.c` | BSD-3-Clause |
| MAME m68000: instruction timing measured and generated into tables | `core/src/cpu/m68k/m68k_cycles_000.inc` | BSD-3-Clause |
| Musashi by Karl Stenerud: base 68000 cycle table | `core/src/cpu/m68k/m68k_cycles_musashi.inc` | MIT |

## Tools used for validation (not distributed)

- MAME, used as a debugger and measuring instrument (`third_party/mame/`, local only).
- SDL2 (zlib license), downloaded separately to build the Windows frontend.

## Brand

The zera330 name, the pixel "z" icon and the capybara mascot belong to the Zera family. Neo Geo is a trademark of SNK Corporation; zera330 is not affiliated with SNK.
