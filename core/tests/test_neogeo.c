/* Teste de fumaça da integração de sistema (Fase 3): 68000 rodando direto
 * do mapa de memória real do neogeo_t (não mais uma RAM sintética solta),
 * escrevendo no latch de som, e o Z80 lendo esse latch via porta de I/O.
 * Prova que a "cola" (core/src/system/neogeo.c) liga as duas CPUs
 * corretamente pro caso mais simples de comunicação entre elas.
 *
 * Programa 68000 (dentro da BIOS sintética, offset 0xC00400): Achado #39
 * (ver docs/ROADMAP.md) corrigiu o mapa de memória real — só os primeiros
 * 128 bytes ($000000-$00007F, a tabela de vetores) são BIOS; de $000080 em
 * diante quem responde é a P ROM do CARTUCHO. Por isso o código do teste
 * synthetic não pode mais morar em $000400 (isso agora seria P ROM, que
 * este teste não popula) — usamos o espelho de código real da BIOS em
 * $C00000-$C1FFFF (onde o hardware de verdade também põe o código, ver
 * comentário de `is_bios_addr` em neogeo.c) em vez disso.
 *   7007            MOVEQ #7,D0
 *   13C0 00320000   MOVE.B D0,$320000     ; REG_SOUND: escreve no latch de som
 *   60FE            BRA *                 ; loop (encerra o teste aqui)
 *
 * Programa Z80: a escrita em REG_SOUND dispara NMI de verdade agora (ver
 * neogeo.c), então o Z80 real pularia pro vetor fixo de NMI (0x0066), não
 * pra 0x0000. O código do Z80 vem da M1 ROM (0x0000-0x7FFF é ROM fixa,
 * confirmado contra a wiki — RAM de verdade só em 0xF800-0xFFFF), então o
 * "programa" tem que ser escrito em `rom.m`, não em `ng.sound_ram`:
 *   0x0000: 76           HALT   (codigo "principal" ocioso, nao deveria nem rodar)
 *   0x0066: DB 00         IN A,(0)   ; le o latch escrito pelo 68000
 *           76            HALT
 */

#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    neogeo_romset_t rom;
    memset(&rom, 0, sizeof(rom));

    rom.bios_size = NEOGEO_BIOS_SIZE;
    rom.bios = (u8 *)calloc(1, rom.bios_size);

    rom.p_size = 4; /* P ROM minima so-pra-nao-ficar-null; nao usada neste teste */
    rom.p = (u8 *)calloc(1, rom.p_size);

    rom.m_size = 4; /* M1 minima, nao usada neste teste (ver audiobios abaixo) */
    rom.m = (u8 *)calloc(1, rom.m_size);

    rom.audiobios_size = 0x100; /* precisa cobrir ao menos ate o offset 0x68 usado abaixo */
    rom.audiobios = (u8 *)calloc(1, rom.audiobios_size);

    if (!rom.bios || !rom.p || !rom.m || !rom.audiobios) {
        printf("FALHA: alocacao\n");
        return 1;
    }

    /* Programa Z80: fica no Audio BIOS (0x0000-0x7FFF), nao na M1 do
     * cartucho nem em sound_ram — ver comentario em neogeo.c sobre o mapa
     * real do Z80 (achado comparando trace-a-trace contra o MAME). */
    /* Achado #89: como no MAME (`machine_reset`), a NMI do Z80 começa
     * DESLIGADA — o driver liga com OUT ($08). */
    rom.audiobios[0x00] = 0xD3; rom.audiobios[0x01] = 0x08; /* OUT ($08),A: liga a NMI */
    rom.audiobios[0x02] = 0x76;                          /* HALT (ocioso) */
    rom.audiobios[0x66] = 0xDB; rom.audiobios[0x67] = 0x00; /* IN A,(0) */
    rom.audiobios[0x68] = 0x76;                          /* HALT */

    /* Vetores de reset do 68000: SSP e PC (big-endian). */
    u32 ssp = 0x00108000u; /* dentro da janela de Work RAM */
    u32 pc0 = 0x00C00400u; /* dentro do espelho de código real da BIOS (Achado #39) */
    rom.bios[0] = (u8)(ssp >> 24); rom.bios[1] = (u8)(ssp >> 16);
    rom.bios[2] = (u8)(ssp >> 8);  rom.bios[3] = (u8)ssp;
    rom.bios[4] = (u8)(pc0 >> 24); rom.bios[5] = (u8)(pc0 >> 16);
    rom.bios[6] = (u8)(pc0 >> 8);  rom.bios[7] = (u8)pc0;

    u8 *p = &rom.bios[0x400];
    p[0] = 0x70; p[1] = 0x07;                     /* MOVEQ #7,D0 */
    p[2] = 0x13; p[3] = 0xC0;                     /* MOVE.B D0,(abs.L) */
    p[4] = 0x00; p[5] = 0x32; p[6] = 0x00; p[7] = 0x00; /* $320000 */
    p[8] = 0x60; p[9] = 0xFE;                     /* BRA * */

    neogeo_t ng;
    neogeo_init(&ng, &rom);
    neogeo_reset(&ng);

    int failures = 0;

    if (ng.m68k.pc != pc0) {
        printf("FALHA: PC 68k pos-reset = 0x%06X, esperado 0x%06X\n", ng.m68k.pc, pc0);
        failures++;
    }

    neogeo_run_slice(&ng, 200);

    if (ng.sound_latch_to_z80 != 7) {
        printf("FALHA: sound_latch_to_z80 = %u, esperado 7\n", ng.sound_latch_to_z80);
        failures++;
    }
    if ((ng.z80.af >> 8) != 7) {
        printf("FALHA: Z80 A = %u, esperado 7 (leu o latch via IN A,(0))\n", ng.z80.af >> 8);
        failures++;
    }
    if (!ng.z80.halted) {
        printf("FALHA: Z80 deveria estar halted\n");
        failures++;
    }
    if (ng.sound_latch_pending) {
        printf("FALHA: sound_latch_pending deveria ter sido limpo pela leitura\n");
        failures++;
    }

    neogeo_romset_free(&rom);

    if (failures == 0) {
        printf("OK: test_neogeo passou (68000 -> latch -> Z80 IN A,(0) = %u)\n", ng.z80.af >> 8);
        return 0;
    }
    printf("test_neogeo FALHOU (%d erro(s))\n", failures);
    return 1;
}
