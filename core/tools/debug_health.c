/* "Saúde" de um jogo em execução longa, pra varrer o acervo atrás de jogos
 * que não rodam: conta exceções de CPU (erro de endereço/barramento,
 * instrução ilegal...), resets do watchdog, quadros com o PC na BIOS e
 * quadros de tela praticamente uniforme; grava o último quadro.
 * Uso: debug_health <zip> <dir_bios> <n_vblanks> <saida.bmp>
 * Saída (1 linha): ok|erro jogo exc=N vet=... wd=N bios=N% escuro=N% cores=N pc=XXXXXX */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_bmp(const char *path, const u8 *rgb, u32 w, u32 h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    u32 row = (w * 3u + 3u) & ~3u, img = row * h, fsz = 54u + img;
    u8 hd[54] = {0};
    hd[0] = 'B'; hd[1] = 'M'; memcpy(hd + 2, &fsz, 4);
    u32 v = 54; memcpy(hd + 10, &v, 4); v = 40; memcpy(hd + 14, &v, 4);
    memcpy(hd + 18, &w, 4); memcpy(hd + 22, &h, 4);
    u16 s = 1; memcpy(hd + 26, &s, 2); s = 24; memcpy(hd + 28, &s, 2); memcpy(hd + 34, &img, 4);
    fwrite(hd, 1, 54, f);
    u8 *line = calloc(1, row);
    for (u32 y = 0; y < h; y++) {
        const u8 *p = rgb + (h - 1 - y) * w * 3u;
        for (u32 x = 0; x < w; x++) { line[x*3] = p[x*3+2]; line[x*3+1] = p[x*3+1]; line[x*3+2] = p[x*3]; }
        fwrite(line, 1, row, f);
    }
    free(line); fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 5) { printf("uso: %s <zip> <dir_bios> <n_vblanks> <saida.bmp>\n", argv[0]); return 1; }
    long n = atol(argv[3]);
    neogeo_romset_t rom; char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) { printf("erro %s\n", err); return 1; }
    static neogeo_t ng;
    neogeo_init(&ng, &rom);
    neogeo_reset(&ng);
    static u8 fb[NEOGEO_SCREEN_W * NEOGEO_SCREEN_H * 3u];
    u32 exc = 0, vecs = 0, bios = 0, dark = 0, samples = 0;
    u32 last_seq = ng.m68k.exception_seq;
    for (long i = 0; i < n; i++) {
        neogeo_run_until_vblank(&ng);
        if (ng.m68k.exception_seq != last_seq) {
            exc += ng.m68k.exception_seq - last_seq;
            if (ng.m68k.last_exception_vector < 32u) vecs |= 1u << ng.m68k.last_exception_vector;
            last_seq = ng.m68k.exception_seq;
        }
        if (ng.m68k.pc >= 0xC00000u) bios++;
        /* uma amostra de tela a cada 60 quadros na segunda metade */
        if (i >= n / 2 && (i % 60) == 0) {
            neogeo_render_frame(&ng, fb);
            u32 lit = 0;
            for (u32 k = 0; k < NEOGEO_SCREEN_W * NEOGEO_SCREEN_H; k++) {
                const u8 *p = fb + k * 3u;
                if (p[0] + p[1] + p[2] > 60) lit++;
            }
            samples++;
            if (lit < (NEOGEO_SCREEN_W * NEOGEO_SCREEN_H) / 50u) dark++;
        }
    }
    neogeo_render_frame(&ng, fb);
    /* cores distintas no último quadro (tela "real" tem dezenas+) */
    static u8 seen[1u << 15];
    memset(seen, 0, sizeof(seen));
    u32 colors = 0;
    for (u32 k = 0; k < NEOGEO_SCREEN_W * NEOGEO_SCREEN_H; k++) {
        const u8 *p = fb + k * 3u;
        u32 key = ((u32)(p[0] >> 3) << 10) | ((u32)(p[1] >> 3) << 5) | (p[2] >> 3);
        if (!seen[key]) { seen[key] = 1; colors++; }
    }
    write_bmp(argv[4], fb, NEOGEO_SCREEN_W, NEOGEO_SCREEN_H);
    const char *g = argv[1];
    for (const char *c = argv[1]; *c; c++) if (*c == '/' || *c == '\\') g = c + 1;
    printf("ok %s exc=%u vet=%08x wd=%u bios=%u%% escuro=%u/%u cores=%u pc=%06x\n",
           g, exc, vecs, ng.watchdog_resets, (u32)(100 * bios / (n ? n : 1)), dark, samples, colors, ng.m68k.pc);
    neogeo_romset_free(&rom);
    return 0;
}
