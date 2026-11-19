/* Mede quantos ciclos do 68000 (e quantos vblanks) cada chamada de
 * neogeo_run_frame_render executa — o caminho usado pelo frontend SDL.
 * Uso: check_frame_speed <cart> <bios> <frames> */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc < 4) return 1;
    static neogeo_romset_t rom; char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) { printf("erro: %s\n", err); return 1; }
    static neogeo_t ng; neogeo_init(&ng, &rom); neogeo_reset(&ng);
    static u8 fb[320 * 224 * 3];
    long n = atol(argv[3]);
    u64 c0 = ng.master_cycles;
    for (long i = 0; i < n; i++) neogeo_run_frame_render(&ng, fb);
    double frames_emulados = (double)(ng.master_cycles - c0) / 202752.0;
    printf("%ld chamadas -> %.2f frames emulados (%.1f%% da velocidade)\n", n, frames_emulados, 100.0 * frames_emulados / (double)n);
    neogeo_romset_free(&rom);
    return 0;
}
