# Cores de CPU (68000 e Z80) portados do projeto Mega Drive do próprio autor
# (C:\ELETRONICA\MEGADRIVE\STM32F429_lMD\third_party\{m68k,z80}), licença MIT
# própria — ver core/src/cpu/LICENSE_M68K_Z80.txt. São cores maduros/testados
# (68000 ~95% do conjunto de instruções; Z80 100%, io ports incluído) que
# vamos adaptar para o mapa de memória e periféricos do Neo-Geo nas próximas
# fases (ver docs/ROADMAP.md).
add_library(neocore STATIC
    src/cpu/m68k/m68k_core.c
    src/cpu/z80/z80.c
    src/system/neogeo.c
    src/system/rom_loader.c
    src/system/prot_sma_kof99.c
    src/system/prot_cmc50.c
    src/system/upd4990a.c
    src/video/lspc_fix.c
    src/video/lspc_sprites.c
    src/audio/ym2610.c
    ../third_party/miniz/miniz.c
)

target_include_directories(neocore PUBLIC include)
# Os arquivos .c fazem #include "m68k_core.h" / #include "z80.h" sem prefixo
# (como no projeto de origem), então o próprio diretório neocore/ também
# precisa estar no include path — só para a biblioteca, não pros consumidores
# (que devem usar #include "neocore/m68k_core.h" etc.).
target_include_directories(neocore PRIVATE include/neocore)

# miniz (Copyright Rich Geldreich, MIT — ver third_party/miniz/LICENSE):
# vendorizado pra ler ROM direto de dentro de um .zip (romset MAME padrão,
# DEFLATE), sem precisar extrair cada jogo numa pasta antes de jogar — ver
# rom_loader.c, camada `rom_source_t`.
target_include_directories(neocore PRIVATE ../third_party/miniz)

# HOST_BUILD desliga o atributo de seção .RamFunc (específico do STM32) e o
# `#pragma GCC optimize` (não reconhecido pelo MSVC) nos arquivos portados.
target_compile_definitions(neocore PUBLIC HOST_BUILD=1)

add_executable(test_m68k_core tests/test_m68k_core.c)
target_link_libraries(test_m68k_core PRIVATE neocore)
add_test(NAME test_m68k_core COMMAND test_m68k_core)

add_executable(test_z80 tests/test_z80.c)
target_link_libraries(test_z80 PRIVATE neocore)
add_test(NAME test_z80 COMMAND test_z80)

add_executable(test_neogeo tests/test_neogeo.c)
target_link_libraries(test_neogeo PRIVATE neocore)
add_test(NAME test_neogeo COMMAND test_neogeo)

add_executable(test_video tests/test_video.c)
target_link_libraries(test_video PRIVATE neocore)
add_test(NAME test_video COMMAND test_video)

add_executable(test_sprites tests/test_sprites.c)
target_link_libraries(test_sprites PRIVATE neocore)
add_test(NAME test_sprites COMMAND test_sprites)

# Ferramenta de diagnostico (nao entra no ctest) - ver core/tools/debug_boot.c
add_executable(debug_boot tools/debug_boot.c)
target_link_libraries(debug_boot PRIVATE neocore)

add_executable(trace_boot tools/trace_boot.c)
target_link_libraries(trace_boot PRIVATE neocore)

add_executable(trace_z80 tools/trace_z80.c)
target_link_libraries(trace_z80 PRIVATE neocore)

add_executable(debug_vblank tools/debug_vblank.c)
target_link_libraries(debug_vblank PRIVATE neocore)

add_executable(debug_bp tools/debug_bp.c)
target_link_libraries(debug_bp PRIVATE neocore)

add_executable(debug_checkpoints tools/debug_checkpoints.c)
target_link_libraries(debug_checkpoints PRIVATE neocore)

add_executable(debug_screenshot tools/debug_screenshot.c)
target_link_libraries(debug_screenshot PRIVATE neocore)

add_executable(debug_wramdump tools/debug_wramdump.c)
target_link_libraries(debug_wramdump PRIVATE neocore)

add_executable(debug_compare tools/debug_compare.c)
target_link_libraries(debug_compare PRIVATE neocore)

add_executable(debug_tile_grid tools/debug_tile_grid.c)
target_link_libraries(debug_tile_grid PRIVATE neocore)

add_executable(debug_perf tools/debug_perf.c)
target_link_libraries(debug_perf PRIVATE neocore)

add_executable(debug_flicker tools/debug_flicker.c)
target_link_libraries(debug_flicker PRIVATE neocore)

add_executable(debug_audio tools/debug_audio.c)
target_link_libraries(debug_audio PRIVATE neocore)

add_executable(test_savestate tools/test_savestate.c)
target_link_libraries(test_savestate PRIVATE neocore)

add_executable(debug_health tools/debug_health.c)
target_link_libraries(debug_health PRIVATE neocore)

add_executable(debug_ym_replay tools/debug_ym_replay.c)
target_link_libraries(debug_ym_replay PRIVATE neocore)

add_executable(debug_wram_at tools/debug_wram_at.c)
target_link_libraries(debug_wram_at PRIVATE neocore)

add_executable(debug_jump tools/debug_jump.c)
target_link_libraries(debug_jump PRIVATE neocore)





add_executable(check_frame_speed tools/check_frame_speed.c)
target_link_libraries(check_frame_speed PRIVATE neocore)
