# Changelog

## 0.1.0 — development

- Controls and keyboard screen in the launcher: Player 1, Player 2 and Shortcuts tabs, a drawn arcade panel that lights up live as keys and controller buttons are pressed (click a part to remap it), up to 4 keys and 4 buttons per input, controller choice per player, duplicate warnings. Uses the game's own SDL2.dll, so it sees controllers exactly as the game does.
- Video and sound settings: window size, 4:3 or square pixels or stretch, smooth filter, scanlines, start in fullscreen, fps in the title, volume, mute.
- New shortcuts in the game: pause (P), reset (F3), fullscreen (F11 or Alt+Enter), screenshot (F12). The game window shows the game's name.
- The console window no longer opens when a game starts from the launcher; the output still goes to the log.
- Launcher `Zera330.exe` in the Zera family layout: banner with the logo, game list (title, set, year, maker, play time, file), Jogar / Ver log / Pasta de saves / Criar atalho / Recarregar / Configurações, portable settings in `user\config`. The emulator is now `zera330c.exe`, and both go to `build\bin`.
- The zera330 opening (3 s, with the coin sound) plays before every game, from `assets/zera330_intro.z3i`. It is decoded with miniz, so it needs no video library on Windows or Linux ARM. Any key skips it.
- Name the project zera330, part of the Zera family: the frontend is now `Zera330.exe`, with the pixel "z" icon in the window and in the executable.
- Fix pgoal running about 20 times slower than normal: every read from the P2 bank area asked the environment for a debug flag.
- Save states (F5/F7) with game signature check; the state of another game is refused.
- Battery-backed RAM saved per game in `nvram/`.
- Controls for two players, keyboard and up to two game controllers with hotplug.
- 68000: address registers keep all 32 bits (s1945p); E-clock synchronization on autovectored interrupts; microcode-exact DIVU/DIVS timing; 34-cycle reset.
- System: the HC259 latch and cartridge vectors reset on soft reset (fatfury3, mutnat); a halted 68000 still lets time pass (viewpoin).
- Video: sprite rows beyond the tile height come from the zoom table as in MAME (samsho3); C ROM regions with gaps load correctly (kof95, whp, kizuna).
- Sound: YM2610 envelope, panning and ADPCM fixes validated against ymfm.
- Earlier work is recorded as Achados #1–#110 in docs/ROADMAP.md.
