# Arquitetura do Emulador Neo-Geo (neogeo-emu)

## Decisões já tomadas (não reabrir sem motivo forte)

- **Linguagem**: C (C11), sem dependências de C++ no core.
- **Fidelidade**: core ciclo-preciso (timing de barramento/instrução do 68000 e Z80,
  timing de scanline do vídeo, timing de amostra do YM2610).
- **Alvos**: Windows (SDL2) + SBCs Linux ARM (Allwinner/Rockchip). ESP32/MCU está
  **fora de escopo** por ora (sem RAM/throughput para cycle-accurate em tempo real).
- **CPU 68000 e Z80**: **portadas do próprio projeto Mega Drive do autor**
  (`C:\ELETRONICA\MEGADRIVE\STM32F429_lMD\third_party\{m68k,z80}`, licença MIT
  própria, preservada em `core/src/cpu/LICENSE_M68K_Z80.txt`). Não são
  terceiros externos — é código do próprio usuário, já maduro:
  - 68000 (`m68k_core.c`/`.h`): ~95% do conjunto de instruções do M68000,
    todos os 12 modos de endereçamento, exceções (address/illegal/zero
    divide/CHK/TRAPV/privilege violation/line 1010/1111), IRQ autovetorado
    níveis 0-7. Timing é aproximado, não ciclo-a-ciclo real ainda — precisa
    de ajuste de tabela de ciclos por modo de EA pra bater com a meta de
    cycle-accurate deste projeto (ver TODO na Fase 1 do roadmap).
  - Z80 (`z80.c`/`.h`): descrito como cycle-accurate no projeto de origem,
    100% das instruções + prefixos CB/DD/ED/FD, IM 0/1/2, NMI, portas de I/O
    (`io_read`/`io_write` — essencial pro Neo-Geo, que usa OUT/IN pro
    YM2610 e bankswitch de som).
  - Ambos usam `#define HOST_BUILD` pra desligar particularidades do STM32
    (atributo de seção `.RamFunc`, `#pragma GCC optimize`) na build desktop;
    o mesmo mecanismo deve funcionar sem alteração ao cross-compilar pro SBC
    Linux ARM.
  - Testados com um smoke test cada (`core/tests/test_m68k_core.c`,
    `core/tests/test_z80.c`) — programinhas montados à mão, não é cobertura
    do conjunto completo. Antes de confiar neles pra rodar jogos reais de
    Neo-Geo, vale rodar contra test suites de referência (Tom Harte 68000
    tests, por exemplo — os próprios docs de origem já recomendavam isso).
  - **O que muda pro Neo-Geo**: o mapa de memória, os vetores de IRQ usados
    (Neo-Geo usa IRQ nível 1 pro vblank/raster do LSPC2, diferente do nível 6
    do Mega Drive), a ausência de VDP/banking do Mega Drive no lado Z80 (o
    Neo-Geo tem seu próprio esquema de bankswitch de ROM de som) e,
    obviamente, o YM2610 no lugar do YM2612+PSG. Nada disso está nos cores
    portados — é tudo trabalho da Fase 3 em diante (mapa de memória
    específico do Neo-Geo, em `core/src/system/`).
- **BIOS/ROMs**: fornecidas pelo usuário localmente (cópias próprias legais). Nunca
  baixadas pela IA, nunca versionadas no repositório (`roms/` está no `.gitignore`).

## Camadas

```
platform/sdl2/        -> main(), janela, framebuffer, áudio, input (SDL2)
        |
        v
core/include/neocore/  -> interfaces públicas do core (headers)
core/src/system/       -> mapa de memória Neo-Geo, cartucho, BIOS, watchdog, NVRAM
core/src/cpu/m68k/     -> CPU principal (jogo + parte do sistema)
core/src/cpu/z80/      -> CPU de som (controla o YM2610)
core/src/video/        -> LSPC2 (fix layer + sprites com zoom por scanline)
core/src/audio/        -> YM2610 (FM + ADPCM-A/B)
```

O core (`core/`) não inclui NADA de SDL2, sockets, threads de plataforma etc. Toda
I/O passa por callbacks/estruturas definidas em `core/include/neocore/`. Isso é o
que permite portar para outra placa: só se reescreve `platform/<nova_placa>/`.

## Modelo de execução (scheduler)

Cycle-accurate multi-chip = não dá pra rodar "68000 até acabar o frame, depois Z80,
depois vídeo". Os chips têm que avançar em pequenos passos intercalados porque um
afeta o outro dentro do mesmo frame (IRQs do vídeo pro 68000, comunicação 68000<->Z80
via latch de som, DMA do LSPC2).

Abordagem: `neo_scheduler_run(ticks)` avança um "mestre de tempo" em unidades de
master clock; a cada tick decide quem roda (68000 roda a cada N ticks, Z80 a cada M
ticks, vídeo avança 1 pixel/tick de pixel clock). Detalhes em
`core/include/neocore/scheduler.h` (a implementar).

## Mapa de memória Neo-Geo (referência, alto nível)

- 68000: BIOS ROM (0x000000), Work RAM (0x100000), regs do LSPC2/paleta/DMA,
  cartucho mapeado (P ROM fixo + banks), latch de comunicação com Z80.
- Z80: Sound ROM do cartucho + bankswitch, Sound RAM, registradores do YM2610,
  latch de comunicação com 68000.
- Vídeo: C ROMs (tiles/sprites) acessadas via LSPC2, paleta em RAM dedicada.

Detalhamento exato por chip vai em `docs/` conforme implementamos cada peça
(vamos precisar disso pra CPU, vídeo e mapa de memória — vou documentar aqui à
medida que cada parte for escrita e validada, não antes, pra não desatualizar).

## Fontes consultadas (Fase 4)

Endereços de registrador, formato de paleta e layout do fix map/tile foram
confirmados contra a NeoGeo Development Wiki (consulta em 2026-09-10):
- [Memory mapped registers](https://wiki.neogeodev.org/index.php?title=Memory_mapped_registers)
- [68k interrupts](https://wiki.neogeodev.org/index.php?title=68k_interrupts)
- [68k/Z80 communication](https://wiki.neogeodev.org/index.php?title=68k%2FZ80_communication)
- [VRAM](https://wiki.neogeodev.org/index.php?title=VRAM)
- [Fix layer](https://wiki.neogeodev.org/index.php/Fix_layer)
- [Fix graphics format](https://wiki.neogeodev.org/index.php/Fix_graphics_format)
- [Colors](https://wiki.neogeodev.org/index.php?title=Colors)

O formato exato do tile fix (fórmula de offset de byte dentro dos 32 bytes)
foi re-derivado por nós a partir de um resumo da página, não copiado
literalmente — ainda não validado visualmente. Ver TODOs em
`core/src/video/lspc_fix.c`.

## Roadmap

Ver `docs/ROADMAP.md`.
