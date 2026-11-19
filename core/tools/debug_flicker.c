/* Roda muitos frames seguidos e detecta "fantasmas" — um frame que diverge
 * MUITO dos vizinhos dos dois lados, mas os vizinhos são parecidos entre si
 * (padrão clássico de um glitch de 1 frame que "some sozinho"). Também
 * dispara com NEOGEO_COIN_FROM_VB/NEOGEO_START_FROM_VB pra tentar alcançar
 * jogo de verdade.
 * Uso: debug_flicker <dir_cartucho> <dir_bios> <vb_inicio> <n_frames> [saida_prefixo]
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 diff_sum(const u8 *a, const u8 *b, u32 n) {
    u32 s = 0;
    for (u32 i = 0; i < n; i++) {
        int d = (int)a[i] - (int)b[i];
        if (d < 0) d = -d;
        s += (u32)d;
    }
    return s;
}

static void write_bmp(const char *path, const u8 *rgb888, u32 w, u32 h) {
    u32 row_bytes = ((w * 3u + 3u) & ~3u);
    u32 img_size = row_bytes * h;
    u32 file_size = 54u + img_size;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    u8 header[54] = {0};
    header[0]='B'; header[1]='M';
    memcpy(&header[2],&file_size,4);
    u32 off=54; memcpy(&header[10],&off,4);
    u32 isz=40; memcpy(&header[14],&isz,4);
    memcpy(&header[18],&w,4);
    s32 sh=(s32)h; memcpy(&header[22],&sh,4);
    u16 pl=1,bpp=24; memcpy(&header[26],&pl,2); memcpy(&header[28],&bpp,2);
    memcpy(&header[34],&img_size,4);
    fwrite(header,1,54,f);
    u8 *row=(u8*)calloc(1,row_bytes);
    for (u32 y=0;y<h;y++){
        u32 sy=h-1-y;
        for (u32 x=0;x<w;x++){
            const u8*p=rgb888+(sy*w+x)*3u;
            row[x*3+0]=p[2]; row[x*3+1]=p[1]; row[x*3+2]=p[0];
        }
        fwrite(row,1,row_bytes,f);
    }
    free(row); fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        printf("uso: %s <dir_cartucho> <dir_bios> <vb_inicio> <n_frames> [saida_prefixo]\n", argv[0]);
        return 1;
    }
    long vb_start = atol(argv[3]);
    long n = atol(argv[4]);
    const char *out_prefix = (argc > 5) ? argv[5] : NULL;

    neogeo_romset_t rom;
    char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) {
        printf("erro: %s\n", err); return 1;
    }
    neogeo_t ng;
    neogeo_init(&ng, &rom);

    const char *backup_file = getenv("NEOGEO_BACKUP_RAM_FILE");
    if (backup_file) {
        FILE *bf = fopen(backup_file, "rb");
        if (bf) {
            u8 raw[NEOGEO_BACKUP_RAM_SIZE];
            size_t rn = fread(raw, 1, sizeof(raw), bf);
            fclose(bf);
            for (size_t i = 0; i + 1 < rn; i += 2) {
                ng.backup_ram[i] = raw[i+1]; ng.backup_ram[i+1] = raw[i];
            }
        }
    }
    neogeo_reset(&ng);

    long coin_from_vb = -1, start_from_vb = -1;
    { const char *e = getenv("NEOGEO_COIN_FROM_VB"); if (e) coin_from_vb = atol(e); }
    { const char *e = getenv("NEOGEO_START_FROM_VB"); if (e) start_from_vb = atol(e); }

    u32 bufsz = NEOGEO_SCREEN_W * NEOGEO_SCREEN_H * 3u;
    u8 *fb_cur  = (u8*)malloc(bufsz);
    u8 *fb_prev = (u8*)malloc(bufsz);
    u8 *fb_prev2= (u8*)malloc(bufsz);
    memset(fb_prev, 0, bufsz);
    memset(fb_prev2, 0, bufsz);

    int found = 0;
    for (long i = 0; i < vb_start + n; i++) {
        if (coin_from_vb >= 0 && i == coin_from_vb) ng.debug_force_coin_frames = 10;
        if (start_from_vb >= 0 && i == start_from_vb) ng.debug_force_start_frames = 10;
        neogeo_run_until_vblank(&ng); /* Achado #54 */
        if (i < vb_start) continue;
        neogeo_render_frame(&ng, fb_cur);

        if (i >= vb_start + 2) {
            u32 d_ab = diff_sum(fb_prev2, fb_prev, bufsz);   /* vizinho-esquerdo vs meio */
            u32 d_bc = diff_sum(fb_prev, fb_cur, bufsz);     /* meio vs vizinho-direito */
            u32 d_ac = diff_sum(fb_prev2, fb_cur, bufsz);    /* os dois vizinhos entre si */
            /* "fantasma": os vizinhos sao parecidos entre si (d_ac baixo),
             * mas o do meio diverge MUITO dos dois (d_ab e d_bc altos) — em
             * PROPORCAO, nao limiar fixo, pra pegar tambem glitches pequenos/
             * localizados durante cenas que ja estao animando (photo, etc). */
            u32 min_ab_bc = (d_ab < d_bc) ? d_ab : d_bc;
            if (min_ab_bc > 3000 && d_ac * 5u < min_ab_bc) {
                printf("FANTASMA em vb=%ld: d_ab=%u d_bc=%u d_ac=%u\n", i - 1, d_ab, d_bc, d_ac);
                if (out_prefix) {
                    char path[512];
                    snprintf(path, sizeof(path), "%s_vb%ld_a.bmp", out_prefix, i-2);
                    write_bmp(path, fb_prev2, NEOGEO_SCREEN_W, NEOGEO_SCREEN_H);
                    snprintf(path, sizeof(path), "%s_vb%ld_GHOST.bmp", out_prefix, i-1);
                    write_bmp(path, fb_prev, NEOGEO_SCREEN_W, NEOGEO_SCREEN_H);
                    snprintf(path, sizeof(path), "%s_vb%ld_c.bmp", out_prefix, i);
                    write_bmp(path, fb_cur, NEOGEO_SCREEN_W, NEOGEO_SCREEN_H);
                }
                found++;
            }
        }
        memcpy(fb_prev2, fb_prev, bufsz);
        memcpy(fb_prev, fb_cur, bufsz);
    }
    printf("fim: %ld frames analisados (vb %ld..%ld), %d fantasma(s) encontrado(s)\n",
           n, vb_start, vb_start + n - 1, found);
    neogeo_romset_free(&rom);
    return 0;
}
