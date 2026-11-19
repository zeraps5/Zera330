/* Valida save state: a instância A roda N vblanks, salva e segue M vblanks; a
 * instância B (outro endereço, estado diferente — rodou outro tanto) carrega o
 * arquivo e roda os mesmos M. Tudo que é observável tem de bater: ciclos, PCs,
 * WRAM, VRAM, paleta, RAM do Z80, fluxo de áudio e a imagem de cada quadro.
 * Uso: test_savestate <zip> <dir_bios> <N> <M> [K_de_B=N/2+37] */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { u32 frames, audio, wram, vram, pal, z80ram; u64 cycles; u32 pc, z80pc; } digest_t;

static u32 fnv(u32 h, const u8 *p, size_t n) { for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u; return h; }

static void run_and_digest(neogeo_t *ng, long m, digest_t *d) {
    static u8 fb[NEOGEO_SCREEN_W * NEOGEO_SCREEN_H * 3u];
    static s16 au[8192 * 2];
    memset(d, 0, sizeof(*d));
    d->frames = d->audio = 2166136261u;
    for (long i = 0; i < m; i++) {
        neogeo_run_frame_render(ng, fb);           /* caminho do frontend */
        d->frames = fnv(d->frames, fb, sizeof(fb));
        u32 n = neogeo_audio_available(ng);
        if (n > 8192u) n = 8192u;
        neogeo_audio_render(ng, au, n);
        d->audio = fnv(d->audio, (const u8 *)au, n * 2u * sizeof(s16));
    }
    d->wram = fnv(2166136261u, ng->wram, sizeof(ng->wram));
    d->vram = fnv(2166136261u, (const u8 *)ng->vram, sizeof(ng->vram));
    d->pal = fnv(2166136261u, (const u8 *)ng->palette, sizeof(ng->palette));
    d->z80ram = fnv(2166136261u, ng->sound_ram, sizeof(ng->sound_ram));
    d->cycles = ng->master_cycles; d->pc = ng->m68k.pc; d->z80pc = ng->z80.pc;
}

int main(int argc, char **argv) {
    if (argc < 5) { printf("uso: %s <zip> <dir_bios> <N> <M> [K]\n", argv[0]); return 2; }
    long n = atol(argv[3]), m = atol(argv[4]);
    long k = argc > 5 ? atol(argv[5]) : n / 2 + 37;
    neogeo_romset_t rom; char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) { printf("erro: %s\n", err); return 2; }
    const char *path = "build/_teste.state";

    neogeo_t *a = calloc(1, sizeof(neogeo_t));
    neogeo_init(a, &rom); neogeo_reset(a);
    for (long i = 0; i < n; i++) neogeo_run_frame_render(a, NULL);
    { u32 x = neogeo_audio_available(a); static s16 t[8192 * 2]; neogeo_audio_render(a, t, x > 8192u ? 8192u : x); }
    if (!neogeo_state_save(a, path)) { printf("FALHA: nao salvou\n"); return 1; }
    digest_t da; run_and_digest(a, m, &da);

    neogeo_t *pad = calloc(1, 12345); /* garante outro endereço pra B */
    neogeo_t *b = calloc(1, sizeof(neogeo_t));
    neogeo_init(b, &rom); neogeo_reset(b);
    for (long i = 0; i < k; i++) neogeo_run_frame_render(b, NULL);
    if (!neogeo_state_load(b, path)) { printf("FALHA: nao carregou\n"); return 1; }
    digest_t db; run_and_digest(b, m, &db);

    int bad = 0;
    #define CMP(f) if (da.f != db.f) { printf("  DIFERE %s: %llx x %llx\n", #f, (unsigned long long)da.f, (unsigned long long)db.f); bad = 1; }
    CMP(frames) CMP(audio) CMP(wram) CMP(vram) CMP(pal) CMP(z80ram) CMP(cycles) CMP(pc) CMP(z80pc)
    printf("%s: salvo no vb%ld, B vinha do vb%ld, %ld quadros depois -> %s (estado %u bytes)\n",
           argv[1], n, k, m, bad ? "DIVERGE" : "IDENTICO", (unsigned)sizeof(neogeo_t));
    if (argc > 6) { /* outro jogo: o estado tem de ser recusado */
        neogeo_romset_t rom2;
        if (neogeo_romset_load_dir_ex(&rom2, argv[6], argv[2], err, sizeof(err))) {
            neogeo_t *c = calloc(1, sizeof(neogeo_t));
            neogeo_init(c, &rom2); neogeo_reset(c);
            u32 pc_antes = c->m68k.pc;
            bool aceitou = neogeo_state_load(c, path);
            printf("carregar estado de %s em %s: %s\n", argv[1], argv[6],
                   aceitou ? "ACEITO (ERRO)" : (c->m68k.pc == pc_antes ? "recusado, estado intacto" : "recusado mas estado mexido (ERRO)"));
            if (aceitou) bad = 1;
            free(c); neogeo_romset_free(&rom2);
        }
    }
    free(pad); free(a); free(b); remove(path);
    neogeo_romset_free(&rom);
    return bad;
}
