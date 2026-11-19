/* Dump de uma grade de tiles de sprite (16x16, C ROM) crus, sem nenhum
 * posicionamento/zoom/paleta de sprite envolvido — só pra inspecionar se o
 * DECODE do tile (pixels) faz sentido visualmente (ex: parece parte de uma
 * letra?), isolando do resto do pipeline de sprites (Achado #42).
 * Uso: debug_tile_grid <dir_cartucho> <dir_bios> <tile_inicial_hex> <qtde> <saida.bmp>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TILE_BYTES_PER_PLANE_PAIR 64u

static u8 tile_pixel(const neogeo_romset_t *rom, u32 tile_code, u32 x, u32 y) {
    if (rom->c_odd_size == 0 || rom->c_even_size == 0) return 0;
    u32 tile_off = tile_code * TILE_BYTES_PER_PLANE_PAIR;
    u32 odd_base = tile_off % rom->c_odd_size;
    u32 even_base = tile_off % rom->c_even_size;
    odd_base -= odd_base % TILE_BYTES_PER_PLANE_PAIR;
    even_base -= even_base % TILE_BYTES_PER_PLANE_PAIR;
    u32 half = (x < 8u) ? 0u : 1u;
    u32 bit = 7u - (x & 7u);
    u32 low_off = half * 32u + y * 2u;
    u32 high_off = low_off + 1u;
    u8 odd_low = rom->c_odd[odd_base + low_off];
    u8 odd_high = rom->c_odd[odd_base + high_off];
    u8 even_low = rom->c_even[even_base + low_off];
    u8 even_high = rom->c_even[even_base + high_off];
    u8 p0 = (u8)((odd_low >> bit) & 1u);
    u8 p1 = (u8)((odd_high >> bit) & 1u);
    u8 p2 = (u8)((even_low >> bit) & 1u);
    u8 p3 = (u8)((even_high >> bit) & 1u);
    return (u8)(p0 | (p1 << 1) | (p2 << 2) | (p3 << 3));
}

int main(int argc, char **argv) {
    if (argc < 6) {
        printf("uso: %s <dir_cartucho> <dir_bios> <tile_inicial_hex> <qtde> <saida.bmp>\n", argv[0]);
        return 1;
    }
    u32 explicit_codes[256];
    u32 n_explicit = 0;
    if (strchr(argv[3], ',')) {
        char buf[2048];
        snprintf(buf, sizeof(buf), "%s", argv[3]);
        char *tok = strtok(buf, ",");
        while (tok && n_explicit < 256) {
            explicit_codes[n_explicit++] = (u32)strtoul(tok, NULL, 16);
            tok = strtok(NULL, ",");
        }
    }
    u32 tile_start = (u32)strtoul(argv[3], NULL, 16);
    u32 count = n_explicit ? n_explicit : (u32)atoi(argv[4]);
    u32 cols_override = (argc >= 7) ? (u32)atoi(argv[6]) : 0;

    neogeo_romset_t rom;
    char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) {
        printf("erro: %s\n", err);
        return 1;
    }

    u32 cols = cols_override ? cols_override : 16u;
    u32 rows = (count + cols - 1u) / cols;
    u32 cell = 16u;
    u32 w = cols * cell;
    u32 h = rows * cell;

    u8 *img = (u8 *)calloc(1, (size_t)w * h * 3u);
    for (u32 t = 0; t < count; t++) {
        u32 gx = (t % cols) * cell;
        u32 gy = (t / cols) * cell;
        u32 code = n_explicit ? explicit_codes[t] : (tile_start + t);
        for (u32 y = 0; y < 16u; y++) {
            for (u32 x = 0; x < 16u; x++) {
                u8 nib = tile_pixel(&rom, code, x, y);
                u8 v = (u8)(nib * 17u); /* 0-15 -> 0-255 em escala de cinza, so pra ver forma */
                u32 px = gx + x, py = gy + y;
                u8 *p = img + ((size_t)py * w + px) * 3u;
                p[0] = p[1] = p[2] = v;
            }
        }
    }

    /* BMP 24-bit simples (igual ao resto das ferramentas) */
    FILE *f = fopen(argv[5], "wb");
    u32 row_bytes = w * 3u;
    u32 pad = (4u - (row_bytes % 4u)) % 4u;
    u32 img_size = (row_bytes + pad) * h;
    u32 file_size = 54u + img_size;
    u8 bmp_header[54] = {0};
    bmp_header[0]='B'; bmp_header[1]='M';
    *(u32*)(bmp_header+2) = file_size;
    *(u32*)(bmp_header+10) = 54u;
    *(u32*)(bmp_header+14) = 40u;
    *(s32*)(bmp_header+18) = (s32)w;
    *(s32*)(bmp_header+22) = -(s32)h; /* top-down */
    *(u16*)(bmp_header+26) = 1;
    *(u16*)(bmp_header+28) = 24;
    *(u32*)(bmp_header+34) = img_size;
    fwrite(bmp_header, 1, 54, f);
    u8 padbuf[4] = {0};
    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            u8 *p = img + ((size_t)y * w + x) * 3u;
            u8 bgr[3] = { p[2], p[1], p[0] };
            fwrite(bgr, 1, 3, f);
        }
        fwrite(padbuf, 1, pad, f);
    }
    fclose(f);
    printf("ok: %u tiles (%04x..%04x) em grade %ux%u, salvo em %s\n", count, tile_start, tile_start+count-1, cols, rows, argv[5]);
    neogeo_romset_free(&rom);
    return 0;
}
