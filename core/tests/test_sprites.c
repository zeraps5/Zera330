/* Teste de fumaça do renderizador de sprites (Fase 4b). Como no teste da
 * fix layer, isso valida a CONSISTÊNCIA INTERNA do nosso pipeline
 * (VRAM/SCB -> decode do C ROM -> paleta -> pixel), não a correção
 * bit-a-bit contra hardware real — ver avisos de confiança no topo de
 * core/src/video/lspc_sprites.c.
 *
 * Monta 1 sprite (índice 0): 1 tile de altura, tamanho cheio (sem shrink),
 * posição (0,0), tile código 5, paleta 2, todo pintado com o índice de cor
 * 5 (dentro da paleta 2). Confere que o pixel (0,0) sai na cor esperada e
 * que um pixel fora da área do sprite continua no fundo (preto).
 */

#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCB1_BASE 0x0000u
#define SCB2_BASE 0x8000u
#define SCB3_BASE 0x8200u
#define SCB4_BASE 0x8400u

int main(void) {
    neogeo_romset_t rom;
    memset(&rom, 0, sizeof(rom));
    rom.bios_size = NEOGEO_BIOS_SIZE; rom.bios = (u8 *)calloc(1, rom.bios_size);
    rom.p_size = 4; rom.p = (u8 *)calloc(1, rom.p_size);
    rom.m_size = 4; rom.m = (u8 *)calloc(1, rom.m_size);
    rom.c_odd_size = 384; rom.c_odd = (u8 *)calloc(1, rom.c_odd_size);
    rom.c_even_size = 384; rom.c_even = (u8 *)calloc(1, rom.c_even_size);

    /* Tile codigo 5 -> offset 5*64=320 em cada malha. Preenche de forma que
     * todo pixel do tile de p0=1,p1=0,p2=1,p3=0 -> nibble = 1|0|4|0 = 5.
     * Layout real (Achado #16, ver docs/ROADMAP.md e lspc_sprites.c): os 2
     * bytes de cada linha dentro de um bloco 8x8 vêm INTERCALADOS (baixo,
     * alto), não agrupados — por isso o byte de índice PAR dentro do tile
     * é sempre o plano baixo e o ÍMPAR o plano alto, não importa o bloco. */
    u32 tile_off = 5u * 64u;
    for (u32 i = 0; i < 64u; i++) {
        u8 v = (u8)((i % 2u) == 0u ? 0xFFu : 0x00u);
        rom.c_odd[tile_off + i] = v;   /* bits 0-1: p0=1 (byte par/baixo), p1=0 (byte impar/alto) */
        rom.c_even[tile_off + i] = v;  /* bits 2-3: p2=1 (byte par/baixo), p3=0 (byte impar/alto) */
    }

    neogeo_t ng;
    neogeo_init(&ng, &rom);
    neogeo_reset(&ng);

    /* SCB1: tile row 0 do sprite 0 = (tile=5, paleta=2, sem flip). */
    ng.vram[SCB1_BASE + 0] = 5;               /* even word: tile code baixo */
    ng.vram[SCB1_BASE + 1] = (u16)(2u << 8);  /* odd word: paleta=2 */

    /* SCB2: sem shrink (tamanho cheio). */
    ng.vram[SCB2_BASE + 0] = 0xFFFFu;

    /* SCB3: altura=1 tile, y_pos=0 -> raw_y=496, sticky=0. */
    ng.vram[SCB3_BASE + 0] = (u16)((496u << 7) | 1u);

    /* SCB4: x_pos=0. */
    ng.vram[SCB4_BASE + 0] = 0;

    /* Paleta 2, cor 5: verde puro. Formato real (Achado #18, ver
     * docs/ROADMAP.md): CrgbRRRRGGGGBBBB. G=31 = g4(bits7-4)=1111 +
     * g_lsb(bit13)=1 -> 0x20F0. */
    ng.palette[2 * 16 + 5] = (u16)(0x20F0u);

    static u8 fb[NEOGEO_SCREEN_W * NEOGEO_SCREEN_H * 3];
    memset(fb, 0, sizeof(fb));
    neogeo_render_sprites(&ng, fb);

    int failures = 0;
    u8 *p00 = fb + (0 * NEOGEO_SCREEN_W + 0) * 3;
    if (p00[0] != 0 || p00[1] != 255 || p00[2] != 0) {
        printf("FALHA: pixel (0,0) = (%u,%u,%u), esperado (0,255,0)\n", p00[0], p00[1], p00[2]);
        failures++;
    }
    u8 *p_out = fb + (0 * NEOGEO_SCREEN_W + 20) * 3; /* fora do sprite (16px de largura) */
    if (p_out[0] != 0 || p_out[1] != 0 || p_out[2] != 0) {
        printf("FALHA: pixel (20,0) deveria ser fundo preto, veio (%u,%u,%u)\n",
               p_out[0], p_out[1], p_out[2]);
        failures++;
    }

    free(rom.bios); free(rom.p); free(rom.m); free(rom.c_odd); free(rom.c_even);

    if (failures == 0) {
        printf("OK: test_sprites passou (sprite sintetico renderizado corretamente)\n");
        return 0;
    }
    printf("test_sprites FALHOU (%d erro(s))\n", failures);
    return 1;
}
