/* Teste de fumaça do vídeo (Fase 4, primeira fatia): protocolo de registro
 * VRAMADDR/VRAMRW/VRAMMOD via mapa de memória real do 68000, e renderização
 * da camada fix a partir do que foi escrito na VRAM.
 *
 * Fluxo: escreve um tile sintético na S ROM, usa os registradores (como um
 * jogo real faria) pra colocar esse tile no fix map, define a paleta,
 * renderiza e confere os pixels resultantes.
 *
 * IMPORTANTE (Achado #28, ver docs/ROADMAP.md): a versão antiga deste teste
 * usava um tile "todo 0x11" — totalmente simétrico, então passava IGUAL com
 * a ordem de nibble certa OU invertida, e também com qualquer permutação das
 * 4 byte-colunas. Ou seja, não pegou nem o bug do Achado #28 (nibbles
 * trocados) nem teria pego o do #17. Agora o tile codifica o índice X em
 * cada pixel (pixel x tem nibble x+1), o que trava a fórmula INTEIRA de
 * `fix_tile_nibble`: ordem das byte-colunas, ordem dos nibbles e o eixo y.
 */

#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    neogeo_romset_t rom;
    memset(&rom, 0, sizeof(rom));
    rom.bios_size = NEOGEO_BIOS_SIZE; rom.bios = (u8 *)calloc(1, rom.bios_size);
    rom.p_size = 4; rom.p = (u8 *)calloc(1, rom.p_size);
    rom.m_size = 4; rom.m = (u8 *)calloc(1, rom.m_size);
    rom.s_size = 64; rom.s = (u8 *)calloc(1, rom.s_size); /* 2 tiles de 32 bytes */

    /* Tile de código 1 (segundo tile, bytes 32-63 da S ROM): pixel x da
     * linha y=0 recebe o nibble (x+1) — valores 1..8, todos distintos, pra
     * travar as 4 byte-colunas E a ordem dos nibbles ao mesmo tempo (ver
     * comentário no topo do arquivo, Achado #28). Byte-colunas reais (de
     * `fix_tile_nibble`): byte_col=2 guarda x{0,1}, byte_col=3 guarda
     * x{2,3}, byte_col=0 guarda x{4,5}, byte_col=1 guarda x{6,7} — e dentro
     * de cada par, x PAR = nibble baixo, x ÍMPAR = nibble alto. byte_off =
     * byte_col*8 + y, então em y=0 os byte_off são 0/8/16/24. */
    rom.s[32 + 16] = 0x21u; /* byte_col=2,y=0 -> x0(baixo)=1, x1(alto)=2 */
    rom.s[32 + 24] = 0x43u; /* byte_col=3,y=0 -> x2(baixo)=3, x3(alto)=4 */
    rom.s[32 + 0]  = 0x65u; /* byte_col=0,y=0 -> x4(baixo)=5, x5(alto)=6 */
    rom.s[32 + 8]  = 0x87u; /* byte_col=1,y=0 -> x6(baixo)=7, x7(alto)=8 */

    neogeo_t ng;
    neogeo_init(&ng, &rom);
    neogeo_reset(&ng);

    /* Paleta (banco 0), entradas 1..8: cada uma com R=G=B=n (4 bits, sem
     * LSB extra/dark) — só precisa ser distinta por entrada, o valor RGB888
     * exato é recalculado abaixo via neogeo_color_to_rgb888 (mesma função
     * que o render usa), em vez de duplicar a conta aqui. */
    for (u32 n = 1; n <= 8u; n++) {
        u16 word = (u16)((n << 8) | (n << 4) | n);
        ng.m68k.write16(ng.m68k.user, 0x400000u + n * 2u, word); /* palette[n] via bus real */
    }

    /* Escreve o fix map word (palette 0, tile 1 = 0x0001) na posicao VRAM
     * $7002 (coluna 0, linha 0 da TELA) usando os registradores, exatamente
     * como o 68000 real faria.
     *
     * Achado #47 (ver docs/ROADMAP.md): a linha do tilemap não é a linha da
     * tela — é a linha de varredura CRUA (`map_idx = col*32 + row +
     * NEOGEO_VBEND_LINES/8`); como a área visível começa na linha crua 16,
     * a linha 0 da tela cai no offset $7002 do tilemap, não $7000. Este
     * teste escrevia em $7000 (linha 0 do tilemap = 16px ANTES da tela
     * visível) desde antes desse achado existir, e nunca foi atualizado —
     * o render simplesmente lia zero (transparente) daquele endereço e a
     * checagem abaixo por acidente não notava porque comparava o endereço
     * errado também. */
    ng.m68k.write16(ng.m68k.user, NEOGEO_REG_VRAMADDR, 0x7002u);
    ng.m68k.write16(ng.m68k.user, NEOGEO_REG_VRAMMOD, 1u);
    ng.m68k.write16(ng.m68k.user, NEOGEO_REG_VRAMRW, 0x0001u);

    int failures = 0;

    if (ng.vram_addr != 0x7003u) {
        printf("FALHA: vram_addr pos-escrita = 0x%04X, esperado 0x7003 (auto-incremento)\n", ng.vram_addr);
        failures++;
    }
    if (ng.vram[0x7002] != 0x0001u) {
        printf("FALHA: vram[0x7002] = 0x%04X, esperado 0x0001\n", ng.vram[0x7002]);
        failures++;
    }

    static u8 fb[NEOGEO_SCREEN_W * NEOGEO_SCREEN_H * 3];
    neogeo_render_fix_layer(&ng, fb);

    /* Confere os 8 pixels da linha y=0 do tile: pixel x deve ter a cor da
     * paleta de índice (x+1) — travando byte-colunas + ordem de nibble. */
    for (u32 x = 0; x < 8u; x++) {
        u32 expected_idx = x + 1u;
        u8 er, eg, eb;
        neogeo_color_to_rgb888(ng.palette[expected_idx], &er, &eg, &eb);
        u8 *px = fb + (0 * NEOGEO_SCREEN_W + x) * 3;
        if (px[0] != er || px[1] != eg || px[2] != eb) {
            printf("FALHA: pixel (%u,0) = (%u,%u,%u), esperado (%u,%u,%u) [paleta idx %u]\n",
                   x, px[0], px[1], px[2], er, eg, eb, expected_idx);
            failures++;
        }
    }
    u8 *p_outside = fb + (0 * NEOGEO_SCREEN_W + 8) * 3; /* proximo tile (coluna 1), nao escrito */
    if (p_outside[0] != 0 || p_outside[1] != 0 || p_outside[2] != 0) {
        printf("FALHA: pixel (8,0) deveria ser fundo preto, veio (%u,%u,%u)\n",
               p_outside[0], p_outside[1], p_outside[2]);
        failures++;
    }

    free(rom.bios); free(rom.p); free(rom.m); free(rom.s);

    if (failures == 0) {
        printf("OK: test_video passou (fix layer renderizou o tile via registradores reais)\n");
        return 0;
    }
    printf("test_video FALHOU (%d erro(s))\n", failures);
    return 1;
}
