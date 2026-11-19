# Contributing to zera330

Corrections, tests, documentation and reproducible compatibility reports are welcome.

## Repository

The public source repository is [zeraps5/Zera330](https://github.com/zeraps5/Zera330), maintained by **metallrockdev**.

1. Fork the repository into your own GitHub account.
2. Clone your fork and create a focused branch from the current `main`.
3. Make the change, preserve credits and licenses, and run the relevant checks below.
4. Open a pull request against `zeraps5/Zera330:main`, describing the behavior, evidence and validation.

Do not commit games, BIOS, cartridge or sound ROMs, battery-backed RAM, save states, local logs, MAME binaries or SDKs. The repository contains no game or BIOS downloads.

## Build

Windows x64 with Visual Studio 2022 (MSVC, C11), CMake 3.16 or newer and Ninja.

1. Download the SDL2 development package for Visual C++ (2.32.x) from <https://github.com/libsdl-org/SDL/releases> and extract it to `third_party/SDL2-2.32.10`.
2. From a "x64 Native Tools" prompt:

```bat
chcp 65001
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`chcp 65001` matters: without it Ninja does not track header dependencies on this setup. The launcher is `build\bin\Zera330.exe` and the emulator `build\bin\zera330c.exe`; without SDL2 the core, tests and tools still build.

## How changes are validated

The core aims to behave exactly like the hardware. MAME is the reference, used as a debugger and measuring instrument, never as a source of copied architecture.

- Read the primary source (MAME driver or device, ymfm) before changing behavior, not a summary.
- Compare against MAME at a fixed frame: WRAM/VRAM dumps (`debug_wram_at`, MAME Lua `-autoboot_script`), screenshots (`debug_screenshot`), CPU traces (`tools/trace_diverge.py`, `tools/frame_offset.sh`) and the YM2610 replay against ymfm (`debug_ym_replay`).
- Run the health sweep (`debug_health` over every romset) before and after core changes and compare the two logs; any game that changes must be explained.
- Record each finding in `docs/ROADMAP.md` as a numbered "Achado" with the measurement that proves it.

A passing CTest does not prove that a game plays correctly. Do not claim gameplay from the attract mode alone.

## Commits and Pull Requests

Keep each change focused. Subject in English, `Area: what changed` (for example `Video: sprite rows past the tile height come from the zoom table`). Explain the problem, the measurement, the tests and what remains open. Preserve credits and license notices; transcribed code keeps its original license (see [CREDITS.md](CREDITS.md)).

## Report an issue

Include the zera330 build, the romset name, the BIOS used, the frame or scene, reproduction steps and the visible symptom. Distinguish a crash, a freeze and slowness.
