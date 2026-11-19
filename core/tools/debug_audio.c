/* Roda N vblanks e grava o áudio gerado num .wav (16-bit estéreo, na taxa
 * nativa do YM2610). Serve pra ouvir/verificar a síntese sem depender do
 * frontend nem de placa de som — mesma ideia do debug_screenshot pro vídeo.
 * Uso: debug_audio <dir_cartucho> <dir_bios> <n_vblanks> <saida.wav>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u32(FILE *f, u32 v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); fputc((v >> 16) & 0xFF, f); fputc((v >> 24) & 0xFF, f); }
static void put_u16(FILE *f, u16 v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); }

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("uso: %s <dir_cartucho> <dir_bios> <n_vblanks> <saida.wav>\n", argv[0]);
        return 1;
    }
    long n_vb = atol(argv[3]);

    neogeo_romset_t rom;
    char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) {
        printf("erro: %s\n", err);
        return 1;
    }
    neogeo_t ng;
    neogeo_init(&ng, &rom);
    neogeo_reset(&ng);
    /* NEOGEO_DUMP_V=<arq>: grava a V ROM do ADPCM-A como carregada (já
     * decifrada) — pro debug_ym_replay e o replay no ymfm original. */
    if (getenv("NEOGEO_DUMP_V") && rom.v) {
        FILE *vf = fopen(getenv("NEOGEO_DUMP_V"), "wb");
        if (vf) { fwrite(rom.v, 1, rom.v_size, vf); fclose(vf); }
    }

    /* Amostras por frame na taxa nativa: 55555 / 59.1856 ≈ 939. */
    const u32 samples_per_frame = NEOGEO_AUDIO_RATE / 59u;
    s16 *buf = (s16 *)malloc(sizeof(s16) * 2u * samples_per_frame * 4u);

    FILE *f = fopen(argv[4], "wb");
    if (!f) { printf("erro: nao consegui criar %s\n", argv[4]); return 1; }
    /* Cabeçalho WAV com tamanhos provisórios (corrigidos no fim). */
    fwrite("RIFF", 1, 4, f); put_u32(f, 0); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put_u32(f, 16); put_u16(f, 1); put_u16(f, 2);
    put_u32(f, NEOGEO_AUDIO_RATE); put_u32(f, NEOGEO_AUDIO_RATE * 4u);
    put_u16(f, 4); put_u16(f, 16);
    fwrite("data", 1, 4, f); put_u32(f, 0);

    u32 total_samples = 0;
    s32 peak = 0;
    for (long i = 0; i < n_vb; i++) {
        neogeo_run_until_vblank(&ng);
        /* Achado #88: grava exatamente o que foi gerado no tempo emulado. */
        u32 n = neogeo_audio_available(&ng);
        if (n > samples_per_frame * 4u) n = samples_per_frame * 4u;
        neogeo_audio_render(&ng, buf, n);
        for (u32 s = 0; s < n * 2u; s++) {
            s32 a = buf[s] < 0 ? -buf[s] : buf[s];
            if (a > peak) peak = a;
        }
        fwrite(buf, sizeof(s16), n * 2u, f);
        total_samples += n;
    }

    u32 data_bytes = total_samples * 4u;
    fseek(f, 4, SEEK_SET);  put_u32(f, 36u + data_bytes);
    fseek(f, 40, SEEK_SET); put_u32(f, data_bytes);
    fclose(f);

    printf("ok: %ld vblanks, %u amostras (%.2fs), pico=%d, escritas no YM=%u -> %s\n",
           n_vb, total_samples, (double)total_samples / (double)NEOGEO_AUDIO_RATE,
           peak, ng.ym_writes, argv[4]);
    free(buf);
    neogeo_romset_free(&rom);
    return 0;
}
