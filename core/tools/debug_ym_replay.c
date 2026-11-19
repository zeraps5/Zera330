/* Reproduz um trace de escritas do YM2610 (NEOGEO_TRACE_YM=1, linhas
 * "[ym] banco X reg $rr = $vv t=<ciclos68k>") só no NOSSO chip, sem CPU,
 * gerando uma amostra a cada 216 ciclos — o mesmo que o replay no ymfm
 * original faz. Serve pra comparar a síntese amostra a amostra.
 * Uso: debug_ym_replay <trace.txt> <saida.wav> */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(FILE *f, unsigned v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, unsigned v) { unsigned short s = (unsigned short)v; fwrite(&s, 2, 1, f); }

int main(int argc, char **argv) {
    if (argc < 3) { printf("uso: %s <trace.txt> <saida.wav>\n", argv[0]); return 1; }
    FILE *in = fopen(argv[1], "r");
    if (!in) { printf("nao abri %s\n", argv[1]); return 1; }
    neogeo_t *ng = calloc(1, sizeof(neogeo_t));
    neogeo_ym2610_reset(ng);
    if (argc > 3) { /* V ROM opcional (NEOGEO_DUMP_V do debug_audio) */
        FILE *vf = fopen(argv[3], "rb");
        if (vf) {
            fseek(vf, 0, SEEK_END); ng->rom.v_size = (u32)ftell(vf); fseek(vf, 0, SEEK_SET);
            ng->rom.v = malloc(ng->rom.v_size);
            if (fread(ng->rom.v, 1, ng->rom.v_size, vf) != ng->rom.v_size) ng->rom.v_size = 0;
            fclose(vf);
        }
    }
    ng->ym_mute_fm = getenv("NEOGEO_MUTE_FM") != NULL;
    ng->ym_mute_adpcm = getenv("NEOGEO_MUTE_ADPCM") != NULL;
    ng->ym_mute_ssg = getenv("NEOGEO_MUTE_SSG") != NULL;

    size_t cap = 1u << 20, n = 0;
    short *pcm = malloc(cap * 4);
    unsigned long long now = 0;
    char line[256];
    s16 buf[2];
    #define GEN_UNTIL(t) while (now + 216u <= (t)) { \
        neogeo_ym2610_generate_sample(ng); neogeo_audio_render(ng, buf, 1); \
        if (n == cap) { cap *= 2; pcm = realloc(pcm, cap * 4); } \
        pcm[n * 2] = buf[0]; pcm[n * 2 + 1] = buf[1]; n++; now += 216u; }
    while (fgets(line, sizeof line, in)) {
        char bank; unsigned reg, val; unsigned long long t;
        if (sscanf(line, "[ym] banco %c reg $%x = $%x t=%llu", &bank, &reg, &val, &t) != 4) continue;
        GEN_UNTIL(t);
        neogeo_ym2610_write_reg(ng, bank == 'B' ? 1u : 0u, (u8)reg, (u8)val);
    }
    { unsigned long long end = now + 216ull * 60000u; GEN_UNTIL(end); }
    FILE *w = fopen(argv[2], "wb");
    unsigned datasz = (unsigned)n * 4u;
    fwrite("RIFF", 1, 4, w); put32(w, 36 + datasz); fwrite("WAVEfmt ", 1, 8, w);
    put32(w, 16); put16(w, 1); put16(w, 2); put32(w, 55555); put32(w, 55555 * 4); put16(w, 4); put16(w, 16);
    fwrite("data", 1, 4, w); put32(w, datasz); fwrite(pcm, 2, n * 2, w); fclose(w);
    printf("%zu amostras -> %s\n", n, argv[2]);
    return 0;
}
